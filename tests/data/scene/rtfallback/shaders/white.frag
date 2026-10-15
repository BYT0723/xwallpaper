varying vec2 v_TexCoord;
uniform sampler2D g_Texture1; // {"default":"util/white"}
void main() {
  gl_FragColor = texture2D(g_Texture1, v_TexCoord);
}
