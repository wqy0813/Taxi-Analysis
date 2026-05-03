import { state } from "../../core/state.js";
import {
    qs,
    renderInfoPanel,
    formatCount,
    formatFloat2,
    formatDateTime,
    parseDateTimeInput,
    requestJson,
    updateModeStatus
} from "../../core/utils.js";
import { stopAllTaxiMode } from "../trajectory/trajectoryService.js";
import { resetDensityState } from "../density/densityStore.js";
import {
    setRegionSelectionMapLocked,
    pixelToPoint
} from "../../map/map.js";

function getSRF() {
    return state.singleRegionFlow;
}

function cloneRegion(region) {
    if (!region) return null;
    return {
        minLon: Number(region.minLon),
        minLat: Number(region.minLat),
        maxLon: Number(region.maxLon),
        maxLat: Number(region.maxLat)
    };
}

function offsetRegion(originRegion, deltaLon, deltaLat) {
    if (!originRegion) return null;
    return {
        minLon: originRegion.minLon + deltaLon,
        minLat: originRegion.minLat + deltaLat,
        maxLon: originRegion.maxLon + deltaLon,
        maxLat: originRegion.maxLat + deltaLat
    };
}

function makePolygon(region) {
    const points = [
        new BMap.Point(region.minLon, region.maxLat),
        new BMap.Point(region.maxLon, region.maxLat),
        new BMap.Point(region.maxLon, region.minLat),
        new BMap.Point(region.minLon, region.minLat)
    ];

    return new BMap.Polygon(points, {
        strokeColor: "#7c5cff",
        strokeWeight: 2,
        strokeOpacity: 0.95,
        fillColor: "#7c5cff",
        fillOpacity: 0.14
    });
}

function lockMapIfNeeded() {
    const srf = getSRF();
    setRegionSelectionMapLocked(Boolean(srf.selecting || srf.dragging));
}

function ensureSelectionLayer() {
    const srf = getSRF();
    if (srf.selectionLayer) {
        return srf.selectionLayer;
    }

    const mapElement = qs("map");
    const layer = document.createElement("div");
    layer.className = "selection-layer";
    layer.dataset.owner = "single-region-flow";
    mapElement.appendChild(layer);
    srf.selectionLayer = layer;
    return layer;
}

function getSelectionBox() {
    return qs("selection-box");
}

function showSelectionBox(startPixel, endPixel) {
    const box = getSelectionBox();
    if (!box) return;

    const left = Math.min(startPixel.x, endPixel.x);
    const top = Math.min(startPixel.y, endPixel.y);
    const width = Math.abs(startPixel.x - endPixel.x);
    const height = Math.abs(startPixel.y - endPixel.y);

    box.style.display = "block";
    box.style.left = `${left}px`;
    box.style.top = `${top}px`;
    box.style.width = `${width}px`;
    box.style.height = `${height}px`;
}

function hideSelectionBox() {
    const box = getSelectionBox();
    if (box) {
        box.style.display = "none";
    }
}

function regionFromPixels(startPixel, endPixel) {
    const leftTop = pixelToPoint(
        Math.min(startPixel.x, endPixel.x),
        Math.min(startPixel.y, endPixel.y)
    );
    const rightBottom = pixelToPoint(
        Math.max(startPixel.x, endPixel.x),
        Math.max(startPixel.y, endPixel.y)
    );

    if (!leftTop || !rightBottom) {
        throw new Error("无法读取框选区域");
    }

    return {
        minLon: Math.min(leftTop.lng, rightBottom.lng),
        minLat: Math.min(leftTop.lat, rightBottom.lat),
        maxLon: Math.max(leftTop.lng, rightBottom.lng),
        maxLat: Math.max(leftTop.lat, rightBottom.lat)
    };
}

function bindPolygonDrag(polygon) {
    if (!polygon) return;
    polygon.addEventListener("mousedown", (event) => {
        if (!event || !event.point) return;
        beginRegionDrag(event.point);
        event.domEvent?.preventDefault?.();
        event.domEvent?.stopPropagation?.();
    });
}

function renderTargetRegion() {
    const srf = getSRF();
    if (srf.polygon) {
        state.map.removeOverlay(srf.polygon);
        srf.polygon = null;
    }
    if (!srf.region) {
        updateRegionInfo();
        return;
    }

    srf.polygon = makePolygon(srf.region);
    bindPolygonDrag(srf.polygon);
    state.map.addOverlay(srf.polygon);
    updateRegionInfo();
}

function startSelectRegion() {
    const srf = getSRF();
    stopAllTaxiMode(true);
    resetDensityState();
    srf.selecting = true;
    srf.selectionStartPixel = null;
    srf.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus("框选目标区域");
}

function cancelSelectRegion() {
    const srf = getSRF();
    srf.selecting = false;
    srf.selectionStartPixel = null;
    srf.selectionEndPixel = null;
    hideSelectionBox();
    lockMapIfNeeded();
}

function beginRegionDrag(point) {
    const srf = getSRF();
    if (srf.selecting || !point || !srf.region) return;
    srf.dragging = true;
    srf.dragStartPoint = { lng: Number(point.lng), lat: Number(point.lat) };
    srf.dragOriginRegion = cloneRegion(srf.region);
    hideSelectionBox();
    lockMapIfNeeded();
    updateModeStatus("拖动目标区域");
}

function updateRegionDrag(point) {
    const srf = getSRF();
    if (!srf.dragging || !srf.dragStartPoint || !srf.dragOriginRegion || !point) return;

    const deltaLon = Number(point.lng) - srf.dragStartPoint.lng;
    const deltaLat = Number(point.lat) - srf.dragStartPoint.lat;
    srf.region = offsetRegion(srf.dragOriginRegion, deltaLon, deltaLat);
    renderTargetRegion();
}

function endRegionDrag() {
    const srf = getSRF();
    if (!srf.dragging) return;
    srf.dragging = false;
    srf.dragStartPoint = null;
    srf.dragOriginRegion = null;
    lockMapIfNeeded();
    updateModeStatus("目标区域已锁定");
}

function clearSingleRegionFlowState() {
    const srf = getSRF();
    cancelSelectRegion();
    if (srf.polygon) {
        state.map.removeOverlay(srf.polygon);
    }
    srf.region = null;
    srf.polygon = null;
    srf.lastResult = null;

    const chart = ensureChart();
    if (chart) {
        chart.setOption(emptyChartOption("等待分析"), true);
    }
    if (srf.modalChart) {
        srf.modalChart.setOption(emptyChartOption("等待分析"), true);
    }
    renderInfoPanel("single-region-flow-info", [], "等待分析");
    updateRegionInfo();
}

function formatRegionText(region) {
    if (!region) return "目标区域未设置";
    return [
        `经度：${region.minLon.toFixed(6)} ~ ${region.maxLon.toFixed(6)}`,
        `纬度：${region.minLat.toFixed(6)} ~ ${region.maxLat.toFixed(6)}`
    ].join("\n");
}

function updateRegionInfo() {
    const srf = getSRF();
    const el = qs("single-region-flow-region-info");
    if (!el) return;
    el.textContent = formatRegionText(srf.region);
    el.classList.toggle("empty", !srf.region);
}

function ensureInputs() {
    const srf = getSRF();
    if (!srf.region) throw new Error("请先框选目标区域");

    const tStart = parseDateTimeInput(qs("single-region-flow-start").value, "开始时间");
    const tEnd = parseDateTimeInput(qs("single-region-flow-end").value, "结束时间");
    if (tEnd <= tStart) {
        throw new Error("结束时间必须晚于开始时间");
    }

    const intervalMinutes = Number(qs("single-region-flow-interval").value);
    const deltaMinutes = Number(qs("single-region-flow-delta").value);

    if (!Number.isFinite(intervalMinutes) || intervalMinutes <= 0) {
        throw new Error("时间粒度无效");
    }
    if (!Number.isFinite(deltaMinutes) || deltaMinutes < 0) {
        throw new Error("ΔT 无效");
    }

    const bucketSize = intervalMinutes * 60;
    return {
        region: srf.region,
        tStart,
        tEnd,
        bucketSize,
        bucketCount: Math.ceil((tEnd - tStart) / bucketSize),
        deltaT: deltaMinutes * 60
    };
}

function ensureChart() {
    const srf = getSRF();
    const el = qs("single-region-flow-chart");
    if (!el || !window.echarts) return null;
    if (!srf.chart) {
        srf.chart = echarts.init(el, null, { renderer: "canvas" });
    }
    return srf.chart;
}

function ensureModalChart() {
    const srf = getSRF();
    const modalEl = srf.modal?.querySelector?.("#single-region-flow-modal-chart");
    if (!modalEl || !window.echarts) return null;
    if (!srf.modalChart) {
        srf.modalChart = echarts.init(modalEl, null, { renderer: "canvas" });
    }
    return srf.modalChart;
}

function emptyChartOption(text = "等待分析") {
    return {
        backgroundColor: "transparent",
        animation: false,
        graphic: {
            type: "text",
            left: "center",
            top: "middle",
            style: {
                text,
                fill: "rgba(102, 120, 136, 0.82)",
                fontSize: 13,
                fontWeight: 500
            }
        }
    };
}

function buildSingleRegionFlowChartOption(data, compact = true) {
    const buckets = data?.buckets || [];
    if (!buckets.length) {
        return emptyChartOption("暂无数据");
    }

    const xData = buckets.map(item => formatDateTime(item.bucketStart));
    const incoming = buckets.map(item => Number(item.incoming || 0));
    const outgoing = buckets.map(item => Number(item.outgoing || 0));

    return {
        backgroundColor: "transparent",
        animation: true,
        animationDuration: compact ? 160 : 220,
        tooltip: {
            trigger: "axis",
            confine: true,
            formatter: (params) => {
                const lines = [params?.[0]?.axisValue || "-"];
                for (const p of (params || [])) {
                    lines.push(`${p.marker}${p.seriesName}: ${Number(p.value).toFixed(2)}`);
                }
                return lines.join("<br/>");
            }
        },
        legend: { top: 6, data: ["流入目标区域", "流出目标区域"] },
        grid: compact
            ? { left: 48, right: 20, top: 42, bottom: 42 }
            : { left: 58, right: 24, top: 54, bottom: 82, containLabel: true },
        xAxis: {
            type: "category",
            data: xData,
            axisLabel: compact
                ? { rotate: 35 }
                : { rotate: 40, hideOverlap: true }
        },
        yAxis: {
            type: "value",
            name: "车流量"
        },
        series: [
            {
                name: "流入目标区域",
                type: "line",
                smooth: true,
                showSymbol: !compact,
                symbolSize: compact ? 0 : 7,
                data: incoming
            },
            {
                name: "流出目标区域",
                type: "line",
                smooth: true,
                showSymbol: !compact,
                symbolSize: compact ? 0 : 7,
                data: outgoing
            }
        ]
    };
}

function renderChart(data) {
    const chart = ensureChart();
    if (!chart) return;
    chart.clear();
    chart.setOption(buildSingleRegionFlowChartOption(data, true), true);
    chart.resize();

    const srf = getSRF();
    if (srf.modalOpen && srf.modalChart) {
        srf.modalChart.clear();
        srf.modalChart.setOption(buildSingleRegionFlowChartOption(data, false), true);
        srf.modalChart.resize();
    }
}

function renderResult(data, params) {
    const summary = data.summary || {};
    renderInfoPanel("single-region-flow-info", [
        ["流入总量", formatFloat2(summary.totalIncoming)],
        ["流出总量", formatFloat2(summary.totalOutgoing)],
        ["净流量", formatFloat2(summary.netFlow)],
        ["开始时间", formatDateTime(params.tStart)],
        ["结束时间", formatDateTime(params.tEnd)],
        ["时间粒度", `${Math.round(params.bucketSize / 60)} 分钟`],
        ["ΔT", `${Math.round(params.deltaT / 60)} 分钟`],
        ["桶数量", formatCount(params.bucketCount)],
        ["接口耗时", `${Number(data.elapsedMs || 0)} ms`]
    ]);
}

async function runSingleRegionFlowQuery() {
    const srf = getSRF();
    const params = ensureInputs();
    const region = params.region;

    const data = await requestJson("/api/region-flow/single", {
        method: "POST",
        body: JSON.stringify({
            minLon: region.minLon,
            minLat: region.minLat,
            maxLon: region.maxLon,
            maxLat: region.maxLat,
            tStart: params.tStart,
            bucketSize: params.bucketSize,
            bucketCount: params.bucketCount,
            deltaT: params.deltaT
        })
    });

    srf.lastResult = data;
    renderResult(data, params);
    renderChart(data);
}

function openSingleRegionFlowModal() {
    const srf = getSRF();
    if (!srf.modal) return;
    srf.modal.hidden = false;
    srf.modalOpen = true;
    if (srf.modalSubtitle) {
        const count = Number(srf.lastResult?.buckets?.length || 0);
        srf.modalSubtitle.textContent = `共 ${formatCount(count)} 个时间段`;
    }

    const modalChart = ensureModalChart();
    if (modalChart) {
        modalChart.clear();
        modalChart.setOption(buildSingleRegionFlowChartOption(srf.lastResult, false), true);
        requestAnimationFrame(() => modalChart.resize());
    }
}

function closeSingleRegionFlowModal() {
    const srf = getSRF();
    if (!srf.modal) return;
    srf.modalOpen = false;
    srf.modal.hidden = true;
}

function bindModalUi() {
    const srf = getSRF();
    qs("single-region-flow-expand")?.addEventListener("click", openSingleRegionFlowModal);
    if (srf.modal) {
        srf.modal.addEventListener("click", (event) => {
            if (event.target?.dataset?.close === "1") {
                closeSingleRegionFlowModal();
            }
        });
    }
    srf.modalClose?.addEventListener("click", closeSingleRegionFlowModal);
    window.addEventListener("keydown", (event) => {
        if (event.key === "Escape" && srf.modalOpen) {
            closeSingleRegionFlowModal();
        }
    });
}

function installSelection() {
    const srf = getSRF();
    const mapElement = qs("map");
    ensureSelectionLayer();

    mapElement.addEventListener("mousedown", (event) => {
        if (!srf.selecting || event.button !== 0) return;
        const rect = mapElement.getBoundingClientRect();
        srf.selectionStartPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        srf.selectionEndPixel = { ...srf.selectionStartPixel };
        showSelectionBox(srf.selectionStartPixel, srf.selectionEndPixel);
        event.preventDefault();
    });

    mapElement.addEventListener("mousemove", (event) => {
        if (!srf.selecting || !srf.selectionStartPixel) return;
        const rect = mapElement.getBoundingClientRect();
        srf.selectionEndPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        showSelectionBox(srf.selectionStartPixel, srf.selectionEndPixel);
        event.preventDefault();
    });

    state.map.addEventListener("mousemove", (event) => {
        if (!srf.dragging || !event?.point) return;
        updateRegionDrag(event.point);
    });

    window.addEventListener("mousemove", (event) => {
        if (srf.selecting && srf.selectionStartPixel) {
            const rect = mapElement.getBoundingClientRect();
            srf.selectionEndPixel = {
                x: event.clientX - rect.left,
                y: event.clientY - rect.top
            };
            showSelectionBox(srf.selectionStartPixel, srf.selectionEndPixel);
            return;
        }
        if (!srf.dragging) return;
        const rect = mapElement.getBoundingClientRect();
        const point = pixelToPoint(event.clientX - rect.left, event.clientY - rect.top);
        if (point) {
            updateRegionDrag(point);
        }
    });

    window.addEventListener("mouseup", (event) => {
        if (srf.dragging) {
            endRegionDrag();
            return;
        }

        if (!srf.selecting || !srf.selectionStartPixel || !srf.selectionEndPixel) return;
        const rect = mapElement.getBoundingClientRect();
        const endPixel = {
            x: event.clientX - rect.left,
            y: event.clientY - rect.top
        };
        const width = Math.abs(srf.selectionStartPixel.x - endPixel.x);
        const height = Math.abs(srf.selectionStartPixel.y - endPixel.y);

        if (width < 8 || height < 8) {
            cancelSelectRegion();
            return;
        }

        const region = regionFromPixels(srf.selectionStartPixel, endPixel);
        cancelSelectRegion();
        srf.region = region;
        renderTargetRegion();
        updateModeStatus("目标区域已锁定");
    });
}

function initSingleRegionFlowFeature() {
    const srf = getSRF();
    srf.modal = qs("single-region-flow-modal");
    srf.modalTitle = qs("single-region-flow-modal-title");
    srf.modalSubtitle = qs("single-region-flow-modal-subtitle");
    srf.modalClose = qs("single-region-flow-modal-close");

    ensureSelectionLayer();
    updateRegionInfo();

    const chart = ensureChart();
    if (chart) {
        chart.setOption(emptyChartOption("等待分析"), true);
    }
    const modalChart = ensureModalChart();
    if (modalChart) {
        modalChart.setOption(emptyChartOption("等待分析"), true);
    }

    installSelection();
    bindModalUi();

    qs("single-region-flow-select-btn")?.addEventListener("click", startSelectRegion);
    qs("single-region-flow-clear-btn")?.addEventListener("click", clearSingleRegionFlowState);
    qs("single-region-flow-btn")?.addEventListener("click", async () => {
        try {
            await runSingleRegionFlowQuery();
        } catch (error) {
            renderInfoPanel("single-region-flow-info", [], error.message);
        }
    });

    window.addEventListener("resize", () => {
        const chartRef = getSRF().chart;
        if (chartRef) {
            chartRef.resize();
        }
        const modalChartRef = getSRF().modalChart;
        if (modalChartRef && getSRF().modalOpen) {
            modalChartRef.resize();
        }
    });
}

export {
    initSingleRegionFlowFeature,
    runSingleRegionFlowQuery,
    startSelectRegion,
    clearSingleRegionFlowState
};
