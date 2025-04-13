$(function () {
    function DryerControlViewModel(parameters) {
        const self = this;
        self.onBeforeBinding = function () {
            self.loadSettings();
            self.fetchState();
            self.fetchHistory();
            setInterval(self.fetchState, 2000);
            setInterval(self.fetchHistory, 5000);
        };

        self.fanOn = ko.observable(false);
        self.heaterOn = ko.observable(false);
        self.fan_pin = ko.observable();
        self.heater_pin = ko.observable();
        self.target_temp = ko.observable();
        self.tolerance = ko.observable();
        self.saveStatus = ko.observable(false);
        self.actualTempDisplay = ko.observable("--");
        self.humidityDisplay = ko.observable("--");


        self.systemOn = ko.observable(false);
        self.tempData = {
            actual_temp: [],
            target_temp: [],
            humidity: []
        };

        self.loadSettings = function () {
            $.get("http://10.0.0.26:8000/settings", function (data) {
                self.fan_pin(data.FAN_PIN);
                self.heater_pin(data.HEATER_PIN);
                self.target_temp(data.TARGET_TEMP);
                self.tolerance(data.TOLERANCE);
            });
        };

        self.saveSettings = function () {
            const settings = {
                FAN_PIN: parseInt(self.fan_pin()),
                HEATER_PIN: parseInt(self.heater_pin()),
                TARGET_TEMP: parseFloat(self.target_temp()),
                TOLERANCE: parseFloat(self.tolerance())
            };

            $.ajax({
                url: "http://10.0.0.26:8000/settings",
                method: "POST",
                contentType: "application/json",
                data: JSON.stringify(settings),
                success: function () {
                    self.saveStatus(true);
                    setTimeout(() => self.saveStatus(false), 2000);
                },
                error: function () {
                    alert("Failed to save settings.");
                }
            });
        };

        self.toggleSystem = function () {
            const newState = !self.systemOn();
            $.ajax({
                url: "http://10.0.0.26:8000/system",
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
            $.get("http://10.0.0.26:8000/system", function (data) {
                self.systemOn(data.system_on);
                self.fanOn(data.fan_on);
                self.heaterOn(data.heater_on);
            });
        };


        let chart;

        self.fetchHistory = function () {
            $.get("http://10.0.0.26:8000/history", function (data) {
                if (!data || data.length === 0) return;

                const latest = data[data.length - 1];
                self.actualTempDisplay(latest.actual_temp.toFixed(1));
                self.humidityDisplay(latest.humidity.toFixed(1));

                const labels = data.map(d => new Date(d.timestamp * 1000).toLocaleTimeString([], {
                    hour: '2-digit',
                    minute: '2-digit'
                }));
                const tempData = data.map(d => d.actual_temp);
                const targetData = data.map(d => d.target_temp);
                const humidityData = data.map(d => d.humidity);

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
