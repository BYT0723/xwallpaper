varying vec2 v_TexCoord;
uniform sampler2D g_Texture0;
uniform float g_Time;
void main() {
  vec4 c = texture2D(g_Texture0, v_TexCoord);
  c.rgb *= 0.5 + 0.5 * sin(g_Time);
  gl_FragColor = c;
}
