#pragma once

#include "../vkh/paths.hpp"
#include "network.hpp"
#include "packet.hpp"
#include <array>
#include <cstring>
#include <fstream>
#include <unordered_map>
#include <uuid/uuid.h>

// Wraps the raw enet Network connection with the client-side protocol logic:
//  - loads (or generates and persists) a client UUID, and sends it exactly
//    once, right after connecting, as a Hello packet. The server uses this
//    to identify us for the rest of the connection; we never send it again,
//    and we're never told our own session id back (the server already
//    excludes us from its own broadcasts, so we don't need to know it).
//  - decodes incoming Join/Update/Leave packets into a map of remote players
//    keyed by their server-assigned session id.
struct RemotePlayer {
  glm::vec3 position{0.f};
  glm::quat orientation{1.f, 0.f, 0.f, 0.f}; // w, x, y, z
};

class NetworkSession {
public:
  explicit NetworkSession(const char *serverAddr) : net(serverAddr) {
    loadOrCreateUuid();
    sendHello();
  }

  // Call once per frame. Drains all pending packets (non-blocking).
  void poll() {
    std::vector<uint8_t> buf;
    while (net.receive(buf, 0)) {
      if (buf.size() < sizeof(Packet))
        continue; // too short to even hold a type + id, malformed

      Packet *header = reinterpret_cast<Packet *>(buf.data());

      switch (header->type) {
      case PacketType::ServerUpdate: {
        if (buf.size() < sizeof(ServerUpdatePacket))
          continue; // malformed, ignore

        ServerUpdatePacket *pkt =
            reinterpret_cast<ServerUpdatePacket *>(buf.data());
        RemotePlayer &p = remotePlayers[pkt->id];
        p.position = pkt->position;
        p.orientation = pkt->orientation;
        break;
      }

      case PacketType::Leave: {
        if (buf.size() < sizeof(LeavePacket))
          continue;

        LeavePacket *pkt = reinterpret_cast<LeavePacket *>(buf.data());

        remotePlayers.erase(pkt->id);
        break;
      }
      default:
        break;
      }
    }
  }

  // Sends our current transform as an Update packet. The server identifies
  // us by connection (we sent our Hello once already), so no id needs to be
  // attached here.
  void sendUpdate(const glm::vec3 &position, const glm::quat &orientation) {
    if (!net.isConnected())
      return;

    ClientUpdatePacket pkt{};
    pkt.type = PacketType::ClientUpdate;
    pkt.position = position;
    pkt.orientation = orientation;

    net.send(&pkt, sizeof(ClientUpdatePacket));
  }

  bool connected() const { return net.isConnected(); }
  const std::unordered_map<uint32_t, RemotePlayer> &players() const {
    return remotePlayers;
  }

private:
  // Loads the persisted UUID from cache/uuid.txt, or generates a fresh one
  // and writes it there if none exists yet (or the file is unreadable).
  void loadOrCreateUuid() {
    paths::initCacheDir("vulkhan");
    const std::filesystem::path uuidPath = paths::cacheDir() / "uuid.txt";

    if (std::ifstream in{uuidPath}; in) {
      std::string hex;
      std::getline(in, hex);
      uuid_t parsed;
      if (!hex.empty() && uuid_parse(hex.c_str(), parsed) == 0) {
        std::memcpy(clientUuid.data(), parsed, clientUuid.size());
        return;
      }
      // file missing/empty/corrupt -- fall through and regenerate below
    }

    uuid_t generated;
    uuid_generate(generated);
    std::memcpy(clientUuid.data(), generated, clientUuid.size());

    char hexOut[37]; // 36 chars + null terminator, per libuuid's format
    uuid_unparse(generated, hexOut);
    if (std::ofstream out{uuidPath, std::ios::trunc}) {
      out << hexOut;
    }
  }

  void sendHello() {
    if (!net.isConnected())
      return;

    HelloPacket pkt{};
    pkt.type = PacketType::Hello;
    std::memcpy(pkt.uuid, clientUuid.data(), clientUuid.size());

    net.send(&pkt, sizeof(HelloPacket), /*reliable=*/true);
  }

  Network net;
  std::array<uint8_t, 16> clientUuid{};
  std::unordered_map<uint32_t, RemotePlayer> remotePlayers;
};
