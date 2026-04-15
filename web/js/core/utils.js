import { state, API_BASE } from "./state.js";

function qs(id) {
    return document.getElementById(id);
}

function setText(id, text) {
    const element = qs(id);
    if (element) {
        element.textContent = text;
    }
}

function setInfoPanel(id, lines, empty = false) {
    const element = qs(id);
    if (!element) {
        return;
    }
    element.classList.toggle("empty", empty);
    element.textContent = lines.join("\n");
}

function escapeHtml(value) {
    return String(value)
        .replaceAll("&", "&amp;")
        .replaceAll("<", "&lt;")
        .replaceAll(">", "&gt;")
        .replaceAll('"', "&quot;")
        .replaceAll("'", "&#39;");
}

function renderInfoPanel(id, rows, emptyText = "等待查询") {
    const element = qs(id);
    if (!element) {
        return;
    }

    if (!rows || rows.length === 0) {
        element.classList.add("empty");
        element.innerHTML = `<div class="info-empty">${escapeHtml(emptyText)}</div>`;
        return;
    }

    element.classList.remove("empty");
    element.innerHTML = `<div class="info-list">${rows.map(([key, value]) => `
        <div class="info-row">
            <span class="info-key">${escapeHtml(key)}</span>
            <span class="info-value">${escapeHtml(value)}</span>
        </div>
    `).join("")}</div>`;
}

function formatCount(value) {
    return Number(value || 0).toLocaleString("zh-CN");
}

function formatDateTime(epochSeconds) {
    const date = new Date(Number(epochSeconds) * 1000);
    const pad = (value) => String(value).padStart(2, "0");
    return `${pad(date.getMonth() + 1)}-${pad(date.getDate())} ${pad(date.getHours())}:${pad(date.getMinutes())}`;
}

async function requestJson(url, options = {}) {
    const response = await fetch(`${API_BASE}${url}`, {
        headers: { "Content-Type": "application/json" },
        ...options
    });
    const text = await response.text();
    if (!text) {
        throw new Error(`请求失败：${response.status} ${response.statusText}`);
    }

    let payload;
    try {
        payload = JSON.parse(text);
    } catch (error) {
        throw new Error(`响应不是有效 JSON：${response.status} ${response.statusText}`);
    }

    if (!payload.success) {
        throw new Error(payload.error?.message || "请求失败");
    }
    return payload.data;
}

function parseDateTimeInput(value, label) {
    if (!value) {
        throw new Error(`${label} 不能为空`);
    }
    const timestamp = Date.parse(value);
    if (Number.isNaN(timestamp)) {
        throw new Error(`${label} 无效`);
    }
    return Math.floor(timestamp / 1000);
}

function ensureRegion() {
    if (!state.region) {
        throw new Error("请先框选区域");
    }
}

function ensureTimeRange(startValue, endValue) {
    const startTime = parseDateTimeInput(startValue, "开始时间");
    const endTime = parseDateTimeInput(endValue, "结束时间");
    if (startTime > endTime) {
        throw new Error("开始时间不能晚于结束时间");
    }
    return { startTime, endTime };
}

function updateMetaStatus() {
    if (!state.meta) {
        return;
    }
    setText("meta-total-points", formatCount(state.meta.totalPoints));
    setText("server-status", "在线");
}

function updateModeStatus(text) {
    setText("stage-status", text);
}

function updateRegionStatus() {
    const badge = qs("region-state-badge");

    if (!state.region) {
        if (badge) {
            badge.textContent = "未选";
            badge.className = "badge muted";
        }
        setText("status-region", "未选");
        renderInfoPanel("region-info", [], "未框选");
        return;
    }

    if (badge) {
        badge.textContent = "已锁定";
        badge.className = "badge";
    }

    const summary = `${state.region.minLon.toFixed(4)}, ${state.region.minLat.toFixed(4)} 路 ${state.region.maxLon.toFixed(4)}, ${state.region.maxLat.toFixed(4)}`;
    setText("status-region", summary);
    renderInfoPanel("region-info", [
        ["经度", `${state.region.minLon.toFixed(6)} ~ ${state.region.maxLon.toFixed(6)}`],
        ["纬度", `${state.region.minLat.toFixed(6)} ~ ${state.region.maxLat.toFixed(6)}`]
    ]);
}

export {
    qs,
    setText,
    setInfoPanel,
    escapeHtml,
    renderInfoPanel,
    formatCount,
    formatDateTime,
    requestJson,
    parseDateTimeInput,
    ensureRegion,
    ensureTimeRange,
    updateMetaStatus,
    updateModeStatus,
    updateRegionStatus
};
