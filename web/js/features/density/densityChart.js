import { state } from "../../core/state.js";
import { qs, escapeHtml, formatCount } from "../../core/utils.js";
import { getBucketLabel, getBucketShortLabel, loadCellTrend } from "./densityStore.js";

function ensureDensityTrendUi() {
    if (!state.densityTrendUiBound) {
        bindDensityTrendUi();
        state.densityTrendUiBound = true;
    }
    ensureDensityTrendCharts();
}

function bindDensityTrendUi() {
    const expandButton = qs("density-trend-expand");
    if (expandButton) {
        expandButton.addEventListener("click", (event) => {
            event.stopPropagation();
            openDensityTrendModal();
        });
    }

    const modal = qs("density-trend-modal");
    if (modal) {
        modal.addEventListener("click", (event) => {
            if (event.target?.dataset?.close === "1") {
                closeDensityTrendModal();
            }
        });
    }

    if (state.densityTrendModalClose) {
        state.densityTrendModalClose.addEventListener("click", closeDensityTrendModal);
    }

    window.addEventListener("keydown", (event) => {
        if (event.key === "Escape" && state.densityTrendModalOpen) {
            closeDensityTrendModal();
        }
    });
}

function ensureDensityTrendCharts() {
    ensureDensityTrendPreviewChart();
    ensureDensityTrendModalChart();
}

function ensureDensityTrendPreviewChart() {
    if (state.densityTrendPreviewChart || !state.densityTrendPreviewEl || !window.echarts) {
        return;
    }

    state.densityTrendPreviewChart = echarts.init(state.densityTrendPreviewEl, null, {
        renderer: "canvas"
    });
    state.densityTrendPreviewChart.setOption(buildDensityTrendEmptyOption("请选择网格查看趋势"), true);
}

function ensureDensityTrendModalChart() {
    if (state.densityTrendModalChart || !state.densityTrendModalEl || !window.echarts) {
        return;
    }

    state.densityTrendModalChart = echarts.init(state.densityTrendModalEl, null, {
        renderer: "canvas"
    });
    state.densityTrendModalChart.setOption(buildDensityTrendEmptyOption("请选择网格查看趋势"), true);
}

function resizeDensityTrendCharts() {
    if (state.densityTrendPreviewChart) {
        state.densityTrendPreviewChart.resize();
    }
    if (state.densityTrendModalChart && state.densityTrendModalOpen) {
        state.densityTrendModalChart.resize();
    }
}

function clearDensityTrendPreview() {
    if (!state.densityTrendPreviewChart) {
        return;
    }
    state.densityTrendPreviewChart.clear();
    state.densityTrendPreviewChart.setOption(buildDensityTrendEmptyOption("请选择网格查看趋势"), true);
}

function clearDensityTrendModal() {
    if (!state.densityTrendModalChart) {
        return;
    }
    state.densityTrendModalChart.clear();
    state.densityTrendModalChart.setOption(buildDensityTrendEmptyOption("请选择网格查看趋势"), true);
}

function buildDensityTrendEmptyOption(emptyText = "请选择网格查看趋势") {
    return {
        backgroundColor: "transparent",
        animation: false,
        grid: {
            left: 42,
            right: 16,
            top: 18,
            bottom: 16,
            containLabel: false
        },
        graphic: {
            type: "text",
            left: "center",
            top: "middle",
            style: {
                text: emptyText,
                fill: "rgba(102, 120, 136, 0.82)",
                fontSize: 12,
                fontWeight: 500
            }
        }
    };
}

function buildDensityTrendDataset(cellKey) {
    if (!state.densityMeta) return { labels: [], data: [], hasData: false };
    const points = [];
    for (let i = 0; i < state.densityMeta.bucketCount; i++) {
        const bucket = state.densityBucketCache.get(i);
        const cell = bucket?.cells?.find(c => `${c.gx}:${c.gy}` === cellKey) || null;
        points.push({
            bucket: state.densityMeta.buckets?.[i],
            cell,
            hasCell: Boolean(cell)
        });
    }
    return {
        labels: points.map(({ bucket }) => getBucketShortLabel(bucket)),
        fullLabels: points.map(({ bucket }) => getBucketLabel(bucket)),
        densityValues: points.map(({ cell }) => Number(cell?.vehicleDensity || 0)),
        cells: points.map(({ cell }) => cell),
        hasData: points.some(({ hasCell }) => hasCell)
    };
}

function buildDensityPreviewOption(trend) {
    if (!trend || !trend.labels || trend.labels.length === 0 || !trend.hasData) {
        return buildDensityTrendEmptyOption("请选择网格查看趋势");
    }

    return {
        backgroundColor: "transparent",
        animation: true,
        animationDuration: 160,
        animationEasing: "cubicOut",
        tooltip: {
            trigger: "axis",
            axisPointer: {
                type: "line",
                lineStyle: {
                    color: "rgba(96, 125, 139, 0.34)",
                    width: 1
                }
            },
            confine: true,
            backgroundColor: "rgba(255, 255, 255, 0.98)",
            borderColor: "rgba(136, 163, 183, 0.16)",
            borderWidth: 1,
            textStyle: {
                color: "#173447"
            },
            extraCssText: "box-shadow: 0 12px 28px rgba(32, 68, 96, 0.10); border-radius: 0;",
            formatter: (params) => {
                const first = params?.[0];
                const dataIndex = first?.dataIndex ?? 0;
                const cell = trend.cells[dataIndex];
                if (!cell) {
                    return "";
                }
                const rows = [];
                rows.push(`<div style="font-weight:600;margin-bottom:6px;">${escapeHtml(trend.labels[dataIndex] || "-")}</div>`);
                rows.push(`<div>车辆密度：<b>${escapeHtml(Number(cell.vehicleDensity || 0).toFixed(2))}</b></div>`);
                return rows.join("");
            }
        },
        grid: {
            left: 32,
            right: 12,
            top: 12,
            bottom: 10,
            containLabel: false
        },
        xAxis: {
            type: "category",
            boundaryGap: false,
            data: trend.labels,
            axisTick: {
                show: false
            },
            axisLine: {
                lineStyle: {
                    color: "rgba(136, 163, 183, 0.18)"
                }
            },
            axisLabel: {
                show: false,
                color: "#667888",
                fontSize: 10
            },
            splitLine: {
                show: false
            }
        },
        yAxis: {
            type: "value",
            name: "车辆密度",
            nameTextStyle: {
                color: "#667888",
                fontSize: 11,
                padding: [0, 0, 0, 6]
            },
            axisLabel: {
                color: "#667888",
                fontSize: 11
            },
            splitLine: {
                lineStyle: {
                    color: "rgba(136, 163, 183, 0.16)"
                }
            }
        },
        series: [
            {
                name: "车辆密度",
                type: "line",
                smooth: true,
                showSymbol: false,
                symbolSize: 5,
                data: trend.densityValues,
                connectNulls: false,
                lineStyle: {
                    width: 2,
                    color: "#5e7cff"
                },
                itemStyle: {
                    color: "#5e7cff"
                },
                emphasis: {
                    focus: "series"
                }
            }
        ],
        graphic: trend.hasData ? [] : {
            type: "text",
            left: "center",
            top: "middle",
            style: {
                text: "请选择网格查看趋势",
                fill: "rgba(102, 120, 136, 0.82)",
                fontSize: 13,
                fontWeight: 600
            }
        }
    };
}

function buildDensityModalOption(trend) {
    if (!trend || !trend.labels || trend.labels.length === 0 || !trend.hasData) {
        return buildDensityTrendEmptyOption("请选择网格查看趋势");
    }

    const labelCount = trend.labels.length;
    const step = labelCount > 8 ? Math.ceil(labelCount / 6) : 1;

    return {
        backgroundColor: "transparent",
        animation: true,
        animationDuration: 220,
        animationEasing: "cubicOut",
        legend: {
            show: true,
            top: 8,
            left: 16,
            itemWidth: 12,
            itemHeight: 8,
            icon: "rect",
            textStyle: {
                color: "#30475a",
                fontSize: 12
            },
            data: ["车辆密度"]
        },
        tooltip: {
            trigger: "axis",
            axisPointer: {
                type: "line"
            },
            confine: true,
            backgroundColor: "rgba(255, 255, 255, 0.98)",
            borderColor: "rgba(136, 163, 183, 0.16)",
            borderWidth: 1,
            textStyle: {
                color: "#173447"
            },
            extraCssText: "box-shadow: 0 16px 36px rgba(32, 68, 96, 0.12); border-radius: 0; padding: 10px 12px;",
            formatter: (params) => {
                const first = params?.[0];
                const dataIndex = first?.dataIndex ?? 0;
                const cell = trend.cells[dataIndex];
                if (!cell) {
                    return "";
                }
                const rows = [];
                rows.push(`<div>时间：<b>${escapeHtml(trend.fullLabels?.[dataIndex] || trend.labels[dataIndex] || "-")}</b></div>`);
                rows.push(`<div>车辆密度：<b>${escapeHtml(Number(cell.vehicleDensity || 0).toFixed(2))}</b></div>`);
                return rows.join("");
            }
        },
        grid: {
            left: 58,
            right: 24,
            top: 54,
            bottom: 82,
            containLabel: true
        },
        xAxis: {
            type: "category",
            boundaryGap: false,
            data: trend.labels,
            axisTick: {
                show: false
            },
            axisLine: {
                lineStyle: {
                    color: "rgba(136, 163, 183, 0.28)"
                }
            },
            axisLabel: {
                color: "#667888",
                fontSize: 11,
                interval: labelCount > 8 ? step - 1 : 0,
                hideOverlap: true,
                rotate: 40,
                formatter: (value) => String(value)
            },
            splitLine: {
                show: false
            }
        },
        yAxis: {
            type: "value",
            name: "车辆密度",
            nameTextStyle: {
                color: "#667888",
                fontSize: 11,
                padding: [0, 6, 0, 0]
            },
            axisLabel: {
                color: "#667888",
                fontSize: 11
            },
            splitLine: {
                lineStyle: {
                    color: "rgba(136, 163, 183, 0.16)"
                }
            }
        },
        series: [
            {
                name: "车辆密度",
                type: "line",
                smooth: true,
                showSymbol: true,
                symbolSize: 7,
                data: trend.densityValues,
                connectNulls: false,
                lineStyle: {
                    width: 3,
                    color: "#f2842c"
                },
                itemStyle: {
                    color: "#f2842c"
                }
            }
        ]
    };
}

async function renderSelectedCellTrend() {
    ensureDensityTrendCharts();
    if (!state.densityTrendPreviewChart) {
        return;
    }

    if (!state.densityMeta || !state.densitySelectedCellKey) {
        clearDensityTrendPreview();
        if (state.densityTrendModalOpen) {
            clearDensityTrendModal();
        }
        return;
    }

    try {
        const trend = await loadCellTrend(state.densitySelectedCellKey);

        if (!trend || !trend.labels.length || !trend.hasData) {
            clearDensityTrendPreview();
            if (state.densityTrendModalOpen) {
                clearDensityTrendModal();
            }
            return;
        }

        state.densityTrendPreviewChart.setOption(buildDensityPreviewOption(trend), true);

        if (state.densityTrendModalOpen) {
            renderDensityTrendModal(trend);
        }
    } catch (error) {
        console.error("加载网格趋势失败:", error);
        clearDensityTrendPreview();
        if (state.densityTrendModalOpen) {
            clearDensityTrendModal();
        }
    }
}

async function renderDensityTrendModal(trend = null) {
    ensureDensityTrendCharts();
    if (!state.densityTrendModalChart) {
        return;
    }

    const selectedKey = state.densitySelectedCellKey;
    if (!selectedKey) {
        clearDensityTrendModal();
        return;
    }

    try {
        const data = trend || await loadCellTrend(selectedKey);

        if (!data || !data.labels.length || !data.hasData) {
            clearDensityTrendModal();
            if (state.densityTrendModalSubtitle) {
                state.densityTrendModalSubtitle.textContent = `网格 ${selectedKey}，共 0 个时间段`;
            }
            return;
        }

        if (state.densityTrendModalSubtitle) {
            state.densityTrendModalSubtitle.textContent = `网格 ${selectedKey}，共 ${formatCount(data.labels.length)} 个时间段`;
        }

        state.densityTrendModalChart.setOption(buildDensityModalOption(data), true);
    } catch (error) {
        console.error("加载趋势弹窗失败:", error);
        clearDensityTrendModal();
    }
}

function openDensityTrendModal() {
    ensureDensityTrendCharts();
    if (!state.densityTrendModal) {
        return;
    }

    state.densityTrendModal.hidden = false;
    state.densityTrendModalOpen = true;
    if (state.densityTrendModalSubtitle) {
        const selectedKey = state.densitySelectedCellKey || "-";
        const trend = selectedKey !== "-" ? buildDensityTrendDataset(selectedKey) : null;
        const count = trend?.labels?.length ? trend.labels.length : 0;
        state.densityTrendModalSubtitle.textContent = `网格 ${selectedKey}，共 ${formatCount(count)} 个时间段`;
    }
    requestAnimationFrame(() => {
        resizeDensityTrendCharts();
        renderDensityTrendModal();
    });
}

function closeDensityTrendModal() {
    if (!state.densityTrendModal) {
        return;
    }

    state.densityTrendModalOpen = false;
    state.densityTrendModal.hidden = true;
}

export {
    ensureDensityTrendUi,
    bindDensityTrendUi,
    ensureDensityTrendCharts,
    ensureDensityTrendPreviewChart,
    ensureDensityTrendModalChart,
    resizeDensityTrendCharts,
    clearDensityTrendPreview,
    clearDensityTrendModal,
    buildDensityTrendEmptyOption,
    buildDensityTrendDataset,
    buildDensityPreviewOption,
    buildDensityModalOption,
    renderSelectedCellTrend,
    renderDensityTrendModal,
    openDensityTrendModal,
    closeDensityTrendModal
};
