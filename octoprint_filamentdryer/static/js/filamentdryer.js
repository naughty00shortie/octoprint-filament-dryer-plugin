$(function () {
    function DryerControlViewModel(parameters) {
        const self = this;
        self.settingsViewModel = parameters[0];

        self.getApiUrl = function() {
            return self.settingsViewModel.settings.plugins.filamentdryer.api_url();
        };

        self.onBeforeBinding = function () {
            self.fetchState();
            self.fetchHistory();
            setInterval(self.fetchState, 2000);
            setInterval(self.fetchHistory, 5000);
        };

        self.fanOn = ko.observable(false);
        self.heaterOn = ko.observable(false);
        self.actualTempDisplay = ko.observable("--");
        self.humidityDisplay = ko.observable("--");


        self.systemOn = ko.observable(false);

        // History cache
        self.historyCache = [];
        self.lastTimestamp = 0;

        self.toggleSystem = function () {
            const newState = !self.systemOn();
            $.ajax({
                url: self.getApiUrl() + "/system",
                method: "POST",
                contentType: "application/json",
                data: JSON.stringify({on: newState}),
                success: function () {
                    self.systemOn(newState);
                },
                error: function () {
                    alert("Failed to toggle system state.");
                }
            });
        };


        self.fetchState = function () {
            $.get(self.getApiUrl() + "/system", function (data) {
                self.systemOn(data.system_on);
                self.fanOn(data.fan_on);
                self.heaterOn(data.heater_on);
            });
        };


        let chart;
        const MAX_HISTORY_POINTS = 1800; // Keep last 30 minutes at 2s intervals

        self.normalizeHistoryEntry = function(entry) {
            // Handle both Python API format (timestamp, actual_temp, target_temp, humidity)
            // and Arduino API format (ts, temp, hum, fan, heater, system, latched)
            let timestamp, temp, targetTemp, humidity;

            if ('timestamp' in entry) {
                // Python API format - timestamp is Unix time in seconds
                timestamp = entry.timestamp * 1000;
                temp = entry.actual_temp;
                targetTemp = entry.target_temp;
                humidity = entry.humidity;
            } else if ('ts' in entry) {
                // Arduino API format - ts is millis() since boot
                // We'll use it as-is for comparison, but convert to wall time for display
                timestamp = entry.ts;
                temp = entry.temp;
                targetTemp = 0; // Arduino doesn't include target in history yet
                humidity = entry.hum;
            }

            return { timestamp, temp, targetTemp, humidity };
        };

        self.fetchHistory = function () {
            // Build URL with since parameter for incremental updates
            let url = self.getApiUrl() + "/history";
            if (self.lastTimestamp > 0) {
                url += "?since=" + self.lastTimestamp;
            }

            $.get(url, function (data) {
                if (!data || data.length === 0) {
                    // No new data, but update display from cache if we have it
                    if (self.historyCache.length > 0) {
                        const latest = self.historyCache[self.historyCache.length - 1];
                        self.actualTempDisplay(latest.temp.toFixed(1));
                        self.humidityDisplay(latest.humidity.toFixed(1));
                    }
                    return;
                }

                // Normalize and append new entries
                const normalizedEntries = data.map(self.normalizeHistoryEntry);
                self.historyCache.push(...normalizedEntries);

                // Trim cache to max size
                if (self.historyCache.length > MAX_HISTORY_POINTS) {
                    self.historyCache = self.historyCache.slice(-MAX_HISTORY_POINTS);
                }

                // Update lastTimestamp from the newest entry
                if (normalizedEntries.length > 0) {
                    self.lastTimestamp = normalizedEntries[normalizedEntries.length - 1].timestamp;
                }

                // Update current readings from latest entry
                const latest = self.historyCache[self.historyCache.length - 1];
                self.actualTempDisplay(latest.temp.toFixed(1));
                self.humidityDisplay(latest.humidity.toFixed(1));

                // Prepare chart data from cache
                const labels = self.historyCache.map(d => {
                    const date = new Date(d.timestamp);
                    return date.toLocaleTimeString([], {
                        hour: '2-digit',
                        minute: '2-digit'
                    });
                });
                const tempData = self.historyCache.map(d => d.temp);
                const targetData = self.historyCache.map(d => d.targetTemp);
                const humidityData = self.historyCache.map(d => d.humidity);

                if (!chart) {
                    const ctx = document.getElementById("dryerChart").getContext("2d");
                    chart = new Chart(ctx, {
                        type: "line",
                        data: {
                            labels: labels,
                            datasets: [
                                {
                                    label: "Actual Temp (°C)",
                                    borderColor: "red",
                                    data: tempData,
                                    fill: false
                                },
                                {
                                    label: "Target Temp (°C)",
                                    borderColor: "#888",
                                    data: targetData,
                                    fill: false
                                },
                                {
                                    label: "Humidity (%)",
                                    borderColor: "blue",
                                    data: humidityData,
                                    fill: false
                                }
                            ]
                        },
                        options: {
                            animation: false,
                            responsive: true,
                            maintainAspectRatio: false,
                            scales: {
                                x: {
                                    ticks: {
                                        maxTicksLimit: 8,
                                        autoSkip: true
                                    }
                                },
                                y: {
                                    beginAtZero: true
                                }
                            }
                        }
                    });
                } else {
                    chart.data.labels = labels;
                    chart.data.datasets[0].data = tempData;
                    chart.data.datasets[1].data = targetData;
                    chart.data.datasets[2].data = humidityData;
                    chart.update();
                }
            });
        };


    }

    OCTOPRINT_VIEWMODELS.push({
        construct: DryerControlViewModel,
        dependencies: ["settingsViewModel", "temperatureViewModel"],
        elements: [
            "#navbar_plugin_filamentdryer",
            "#tab_plugin_filamentdryer",
            "#settings_plugin_filamentdryer"
        ]
    });
});
