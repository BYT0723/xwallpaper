export async function paint() {
  const r = await fetch('data.json');
  const j = await r.json();
  document.body.style.background = j.bg;
  document.body.textContent = j.text;
}
