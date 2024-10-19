#pragma once

#include <bitset>
#include <queue>
#include <set>
#include <array>
#include <unordered_map>
#include <memory>

#include <cassert>

namespace vkh {
	// A simple type alias
	using Entity = std::uint32_t;

	// Used to define the size of arrays later on
	const Entity MAX_ENTITIES = 5000;

	// A simple type alias
	using ComponentType = std::uint8_t;

	// Used to define the size of arrays later on
	const ComponentType MAX_COMPONENTS = 32;

	// A simple type alias
	using Signature = std::bitset<MAX_COMPONENTS>;

	class EntityManager
	{
	public:
		EntityManager()
		{
			// Initialize the queue with all possible entity IDs
			for (Entity entity = 0; entity < MAX_ENTITIES; ++entity)
			{
				availableEntities.push(entity);
			}
		}

		Entity createEntity()
		{
			assert(livingEntityCount < MAX_ENTITIES && "Too many entities in existence.");

			// Take an ID from the front of the queue
			Entity id = availableEntities.front();
			availableEntities.pop();
			++livingEntityCount;

			return id;
		}

		void destroyEntity(Entity entity)
		{
			assert(entity < MAX_ENTITIES && "Entity out of range.");

			// Invalidate the destroyed entity's signature
			signatures[entity].reset();

			// Put the destroyed ID at the back of the queue
			availableEntities.push(entity);
			--livingEntityCount;
		}

		void setSignature(Entity entity, Signature signature)
		{
			assert(entity < MAX_ENTITIES && "Entity out of range.");

			// Put this entity's signature into the array
			signatures[entity] = signature;
		}

		Signature getSignature(Entity entity)
		{
			assert(entity < MAX_ENTITIES && "Entity out of range.");

			// Get this entity's signature from the array
			return signatures[entity];
		}

	private:
		// Queue of unused entity IDs
		std::queue<Entity> availableEntities{};

		// Array of signatures where the index corresponds to the entity ID
		std::array<Signature, MAX_ENTITIES> signatures{};

		// Total living entities - used to keep limits on how many exist
		uint32_t livingEntityCount{};
	};

	// The one instance of virtual inheritance in the entire implementation.
	// An interface is needed so that the ComponentManager (seen later)
	// can tell a generic ComponentArray that an entity has been destroyed
	// and that it needs to update its array mappings.
	class IComponentArray
	{
	public:
		virtual ~IComponentArray() = default;
		virtual void entityDestroyed(Entity entity) = 0;
	};


	template<typename T>
	class ComponentArray : public IComponentArray
	{
	public:
		void insertData(Entity entity, T component)
		{
			assert(entityToIndexMap.find(entity) == entityToIndexMap.end() && "Component added to same entity more than once.");

			// Put new entry at end and update the maps
			size_t newIndex = size;
			entityToIndexMap[entity] = newIndex;
			indexToEntityMap[newIndex] = entity;
			componentArray[newIndex] = component;
			++size;
		}

		void removeData(Entity entity)
		{
			assert(entityToIndexMap.find(entity) != entityToIndexMap.end() && "Removing non-existent component.");

			// Copy element at end into deleted element's place to maintain density
			size_t indexOfRemovedEntity = entityToIndexMap[entity];
			size_t indexOfLastElement = size - 1;
			componentArray[indexOfRemovedEntity] = componentArray[indexOfLastElement];

			// Update map to point to moved spot
			Entity entityOfLastElement = indexToEntityMap[indexOfLastElement];
			entityToIndexMap[entityOfLastElement] = indexOfRemovedEntity;
			indexToEntityMap[indexOfRemovedEntity] = entityOfLastElement;

			entityToIndexMap.erase(entity);
			indexToEntityMap.erase(indexOfLastElement);

			--size;
		}

		T& getData(Entity entity)
		{
			assert(entityToIndexMap.find(entity) != entityToIndexMap.end() && "Retrieving non-existent component.");

			// Return a reference to the entity's component
			return componentArray[entityToIndexMap[entity]];
		}

		void entityDestroyed(Entity entity) override
		{
			if (entityToIndexMap.find(entity) != entityToIndexMap.end())
			{
				// Remove the entity's component if it existed
				removeData(entity);
			}
		}

	private:
		// The packed array of components (of generic type T),
		// set to a specified maximum amount, matching the maximum number
		// of entities allowed to exist simultaneously, so that each entity
		// has a unique spot.
		std::array<T, MAX_ENTITIES> componentArray;

		// Map from an entity ID to an array index.
		std::unordered_map<Entity, size_t> entityToIndexMap;

		// Map from an array index to an entity ID.
		std::unordered_map<size_t, Entity> indexToEntityMap;

		// Total size of valid entries in the array.
		size_t size;
	};

	class ComponentManager
	{
	public:
		template<typename T>
		void registerComponent()
		{
			const char* typeName = typeid(T).name();

			assert(componentTypes.find(typeName) == componentTypes.end() && "Registering component type more than once.");

			// Add this component type to the component type map
			componentTypes.insert({ typeName, nextComponentType });

			// Create a ComponentArray pointer and add it to the component arrays map
			componentArrays.insert({ typeName, std::make_shared<ComponentArray<T>>() });

			// Increment the value so that the next component registered will be different
			++nextComponentType;
		}

		template<typename T>
		ComponentType getComponentType()
		{
			const char* typeName = typeid(T).name();

			assert(componentTypes.find(typeName) != componentTypes.end() && "Component not registered before use.");

			// Return this component's type - used for creating signatures
			return componentTypes[typeName];
		}

		template<typename T>
		void addComponent(Entity entity, T component)
		{
			// Add a component to the array for an entity
			getComponentArray<T>()->insertData(entity, component);
		}

		template<typename T>
		void removeComponent(Entity entity)
		{
			// Remove a component from the array for an entity
			getComponentArray<T>()->removeData(entity);
		}

		template<typename T>
		T& getComponent(Entity entity)
		{
			// Get a reference to a component from the array for an entity
			return getComponentArray<T>()->getData(entity);
		}

		void entityDestroyed(Entity entity)
		{
			// Notify each component array that an entity has been destroyed
			// If it has a component for that entity, it will remove it
			for (auto const& pair : componentArrays)
			{
				auto const& component = pair.second;

				component->entityDestroyed(entity);
			}
		}

	private:
		// Map from type string pointer to a component type
		std::unordered_map<const char*, ComponentType> componentTypes{};

		// Map from type string pointer to a component array
		std::unordered_map<const char*, std::shared_ptr<IComponentArray>> componentArrays{};

		// The component type to be assigned to the next registered component - starting at 0
		ComponentType nextComponentType{};

		// Convenience function to get the statically casted pointer to the ComponentArray of type T.
		template<typename T>
		std::shared_ptr<ComponentArray<T>> getComponentArray()
		{
			const char* typeName = typeid(T).name();

			assert(componentTypes.find(typeName) != componentTypes.end() && "Component not registered before use.");

			return std::static_pointer_cast<ComponentArray<T>>(componentArrays[typeName]);
		}
	};

	class System
	{
	public:
		std::set<Entity> entities;
	};

	class SystemManager
	{
	public:
		template<typename T, typename... Args>
		std::shared_ptr<T> registerSystem(Args&&... args)
		{
			const char* typeName = typeid(T).name();

			assert(systems.find(typeName) == systems.end() && "Registering system more than once.");

			// Create a pointer to the system and return it so it can be used externally
			auto system = std::make_shared<T>(std::forward<Args>(args)...);
			systems.insert({ typeName, system });
			return system;
		}

		template<typename T>
		void setSignature(Signature signature)
		{
			const char* typeName = typeid(T).name();

			assert(systems.find(typeName) != systems.end() && "System used before registered.");

			// Set the signature for this system
			signatures.insert({ typeName, signature });
		}

		void entityDestroyed(Entity entity)
		{
			// Erase a destroyed entity from all system lists
			// mEntities is a set so no check needed
			for (auto const& pair : systems)
			{
				auto const& system = pair.second;

				system->entities.erase(entity);
			}
		}

		void entitySignatureChanged(Entity entity, Signature entitySignature)
		{
			// Notify each system that an entity's signature changed
			for (auto const& pair : systems)
			{
				auto const& type = pair.first;
				auto const& system = pair.second;
				auto const& systemSignature = signatures[type];

				// Entity signature matches system signature - insert into set
				if ((entitySignature & systemSignature) == systemSignature)
				{
					system->entities.insert(entity);
				}
				// Entity signature does not match system signature - erase from set
				else
				{
					system->entities.erase(entity);
				}
			}
		}

	private:
		// Map from system type string pointer to a signature
		std::unordered_map<const char*, Signature> signatures{};

		// Map from system type string pointer to a system pointer
		std::unordered_map<const char*, std::shared_ptr<System>> systems{};
	};

	class ECSManager
	{
	public:
		void Init()
		{
			// Create pointers to each manager
			componentManager = std::make_unique<ComponentManager>();
			entityManager = std::make_unique<EntityManager>();
			systemManager = std::make_unique<SystemManager>();
		}


		// Entity methods
		Entity createEntity()
		{
			return entityManager->createEntity();
		}

		void destroyEntity(Entity entity)
		{
			entityManager->destroyEntity(entity);

			componentManager->entityDestroyed(entity);

			systemManager->entityDestroyed(entity);
		}


		// Component methods
		template<typename T>
		void registerComponent()
		{
			componentManager->registerComponent<T>();
		}

		template<typename T>
		void addComponent(Entity entity, T component)
		{
			componentManager->addComponent<T>(entity, component);

			auto signature = entityManager->getSignature(entity);
			signature.set(componentManager->getComponentType<T>(), true);
			entityManager->setSignature(entity, signature);

			systemManager->entitySignatureChanged(entity, signature);
		}

		template<typename T>
		void removeComponent(Entity entity)
		{
			componentManager->removeComponent<T>(entity);

			auto signature = entityManager->getSignature(entity);
			signature.set(componentManager->getComponentType<T>(), false);
			entityManager->setSignature(entity, signature);

			systemManager->entitySignatureChanged(entity, signature);
		}

		template<typename T>
		T& getComponent(Entity entity)
		{
			return componentManager->getComponent<T>(entity);
		}

		template<typename T>
		ComponentType getComponentType()
		{
			return componentManager->getComponentType<T>();
		}


		// System methods
		template<typename T, typename... Args>
		std::shared_ptr<T> registerSystem(Args&&... args)
		{
			return systemManager->registerSystem<T>(std::forward<Args>(args)...);
		}

		template<typename T>
		void setSystemSignature(Signature signature)
		{
			systemManager->setSignature<T>(signature);
		}

	private:
		std::unique_ptr<ComponentManager> componentManager;
		std::unique_ptr<EntityManager> entityManager;
		std::unique_ptr<SystemManager> systemManager;
	};
}