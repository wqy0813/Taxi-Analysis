import { state } from "./core/state.js";
import { qs, setText, renderInfoPanel, updateMetaStatus, updateRegionStatus, updateModeStatus, requestJson } from "./core/utils.js";
import { loadBaiduMapScript, initMap, clearRegionOverlay, clearTrajectoryOverlays, resetDensityState } from "./map/map.js";
import { ensureDensityOverlay } from "./map/overlays/densityOverlay.js";
import { installDensityMapInteractions } from "./features/density/densityView.js";
import { installRegionSelection, startRegionSelection, cancelRegionSelection } from "./map/regionSelection.js";
import { activateDockPanel, clearDockSelection } from "./ui/dock.js";
import { runTrajectoryQuery } from "./features/trajectory/trajectoryService.js";
import { runRegionQuery } from "./features/region/regionService.js";
import { runDensityQuery, setDensityBucketIndex, stopDensityPlayback, startDensityPlayback } from "./features/density/densityService.js";
import { initRegionFlowFeature } from "./features/regionFlow/regionFlow.js";
import { initSingleRegionFlowFeature } from "./features/regionFlow/singleRegionFlow.js";
function applyDefaultTimeValues() {
    const defaultStart = "2008-02-03T06:30";
    const defaultEnd = "2008-02-03T22:00";
    qs("region-start").value = defaultStart;
    qs("region-end").value = defaultEnd;
    qs("density-start").value = defaultStart;
    qs("density-end").value = defaultEnd;
	qs("region-flow-start").value = defaultStart;
	qs("region-flow-end").value = defaultEnd;
    qs("single-region-flow-start").value = defaultStart;
    qs("single-region-flow-end").value = defaultEnd;
}

function attachButtonRipple(button) {
    button.addEventListener("pointerdown", (event) => {
        const rect = button.getBoundingClientRect();
        const ripple = document.createElement("span");
        ripple.className = "ripple";
        const size = Math.max(rect.width, rect.height) * 1.8;
        ripple.style.width = `${size}px`;
        ripple.style.height = `${size}px`;
        ripple.style.left = `${event.clientX - rect.left}px`;
        ripple.style.top = `${event.clientY - rect.top}px`;
        button.appendChild(ripple);
        ripple.addEventListener("animationend", () => ripple.remove(), { once: true });
    });
}

function bindEvents() {
    document.querySelectorAll(".tool-btn[data-panel]").forEach((button) => {
        button.addEventListener("click", () => {
            activateDockPanel(button.dataset.panel);
        });
    });

    document.querySelectorAll("button").forEach((button) => {
        attachButtonRipple(button);
    });

    const rail = document.querySelector(".rail");
    if (rail) {
        rail.addEventListener("dblclick", (event) => {
            if (event.target.closest("button, input, select, label, .result, .result-block")) {
                return;
            }
            clearDockSelection();
        });
    }

    qs("select-region-btn").addEventListener("click", startRegionSelection);

    qs("clear-region-btn").addEventListener("click", () => {
        cancelRegionSelection();
        state.region = null;
        clearRegionOverlay();
        clearTrajectoryOverlays();
        resetDensityState();
        updateRegionStatus();
    });

    qs("trajectory-btn").addEventListener("click", async () => {
        try {
            await runTrajectoryQuery();
        } catch (error) {
            renderInfoPanel("trajectory-info", [], error.message);
        }
    });

    qs("region-query-btn").addEventListener("click", async () => {
        try {
            await runRegionQuery();
        } catch (error) {
            renderInfoPanel("region-query-info", [], error.message);
        }
    });

    qs("density-btn").addEventListener("click", async () => {
        try {
            await runDensityQuery();
        } catch (error) {
            renderInfoPanel("density-info", [], error.message);
        }
    });

    qs("density-bucket").addEventListener("change", (event) => {
        setDensityBucketIndex(Number(event.target.value));
    });

    qs("density-timeline").addEventListener("input", (event) => {
        setDensityBucketIndex(Number(event.target.value));
    });

    qs("density-prev").addEventListener("click", () => {
        stopDensityPlayback();
        setDensityBucketIndex(state.currentBucketIndex - 1);
    });

    qs("density-next").addEventListener("click", () => {
        stopDensityPlayback();
        setDensityBucketIndex(state.currentBucketIndex + 1);
    });

    qs("density-play").addEventListener("click", () => {
        if (state.densityPlayTimer) {
            stopDensityPlayback();
            return;
        }
        startDensityPlayback();
    });
}

async function bootstrap() {
    setText("server-status", "连接中");
    try {
        state.meta = await requestJson("/api/meta");
        await loadBaiduMapScript(state.meta.baiduMapAk);
        initMap();
        ensureDensityOverlay();
        installDensityMapInteractions();
        installRegionSelection();
		initRegionFlowFeature();
        initSingleRegionFlowFeature();
        bindEvents();
        applyDefaultTimeValues();
        updateMetaStatus();
        updateRegionStatus();
        updateModeStatus("地图");
        clearDockSelection();
    } catch (error) {
        setText("server-status", "失败");
        renderInfoPanel("trajectory-info", [], error.message);
        throw error;
    }
}

bootstrap().catch((error) => {
    console.error(error);
});
