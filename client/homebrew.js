// Manifest per il launcher homebrew (ps5-payload-websrv / etaHEN / OnionHEN).
// Avvia la Omega UI come APP homebrew: così SDL ottiene il contesto video.
async function main() {
    const PAYLOAD = window.workingDir + '/OmegaUI.elf';
    return {
        mainText: "Omega",
        secondaryText: "Omega Network",
        onclick: async () => {
            return { path: PAYLOAD };
        }
    };
}
