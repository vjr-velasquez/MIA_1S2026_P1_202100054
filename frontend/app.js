function $(id) { return document.getElementById(id); }

const EXAMPLE_BASE_PATH = "/home/vjr-velasquez/Documentos/mia-proyecto1/outputs";

function appendOutput(text) {
  const out = $("outputArea");
  out.value += text;
  out.scrollTop = out.scrollHeight;
}

function setHealthStatus(text) {
  $("healthStatus").textContent = text;
}

async function checkHealth() {
  const base = $("backendUrl").value.trim().replace(/\/+$/, "");
  setHealthStatus("consultando...");
  try {
    const res = await fetch(`${base}/health`);
    const txt = await res.text();
    setHealthStatus(`${res.status} ${txt}`);
  } catch (e) {
    setHealthStatus(`ERROR: ${e.message}`);
  }
}

async function runCommands() {
  const base = $("backendUrl").value.trim().replace(/\/+$/, "");
  const commands = $("inputArea").value;

  if (!commands.trim()) {
    appendOutput("ERROR: no hay comandos para ejecutar\n");
    return;
  }

  appendOutput("---- Ejecutando ----\n");

  try {
    const res = await fetch(`${base}/execute`, {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ commands })
    });

    const data = await res.json().catch(() => null);

    if (!data || typeof data.output !== "string") {
      appendOutput(`ERROR: respuesta inválida del backend (status ${res.status})\n`);
      return;
    }

    appendOutput(data.output);
    if (!data.output.endsWith("\n")) appendOutput("\n");
  } catch (e) {
    appendOutput(`ERROR: ${e.message}\n`);
  }
}

function loadFileToInput(file) {
  const reader = new FileReader();
  reader.onload = () => { $("inputArea").value = reader.result ?? ""; };
  reader.onerror = () => { appendOutput("ERROR: no se pudo leer el archivo\n"); };
  reader.readAsText(file);
}

function loadExample() {
  const example =
`# Script ejemplo Sprint 3

mkdisk -size=10 -unit=M -path="${EXAMPLE_BASE_PATH}/disks/d1.mia"
fdisk -size=6 -unit=M -path="${EXAMPLE_BASE_PATH}/disks/d1.mia" -name="EXT" -type=E
fdisk -size=1 -unit=M -path="${EXAMPLE_BASE_PATH}/disks/d1.mia" -name="log1" -type=L
fdisk -size=1 -unit=M -path="${EXAMPLE_BASE_PATH}/disks/d1.mia" -name="log2" -type=L
mount -path="${EXAMPLE_BASE_PATH}/disks/d1.mia" -name="EXT"
rep -name=disk -id=vda1 -path="${EXAMPLE_BASE_PATH}/reports/disk.txt"
rep -name=mbr  -id=vda1 -path="${EXAMPLE_BASE_PATH}/reports/mbr.txt"

# Fin
`;
  $("inputArea").value = example;
}

function main() {
  $("btnHealth").addEventListener("click", checkHealth);
  $("btnRun").addEventListener("click", runCommands);
  $("btnClearOut").addEventListener("click", () => $("outputArea").value = "");
  $("btnLoadExample").addEventListener("click", loadExample);

  $("fileInput").addEventListener("change", (e) => {
    const file = e.target.files && e.target.files[0];
    if (file) loadFileToInput(file);
  });

  checkHealth();
}

main();
