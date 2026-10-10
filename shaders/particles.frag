#version 450
layout(location=0) in vec4 color;
layout(location=1) in vec2 uv;
layout(location=2) flat in float kind;
layout(location=0) out vec4 outColor;
void main() {
  float r2=dot(uv,uv);
  float glow=exp(-r2*5.0)*(1.0-smoothstep(.65,1.0,r2));
  if(kind==1.0) glow*=1.0-smoothstep(.2,.95,abs(uv.x)+uv.y*uv.y*.55);
  if(kind==2.0) {
    vec2 head=vec2(uv.x,uv.y-.3);
    float a=atan(head.y,head.x);
    float edge=.3+.1*cos(a*5.0);
    float flower=1.0-smoothstep(edge-.04,edge+.04,length(head));
    float stem=(1.0-smoothstep(.02,.06,abs(uv.x)))*(1.0-smoothstep(-.1,.2,uv.y));
    glow=flower+stem*.15+glow*.15;
  }
  if(glow<.001) discard;
  outColor=vec4(color.rgb,color.a*glow);
}
