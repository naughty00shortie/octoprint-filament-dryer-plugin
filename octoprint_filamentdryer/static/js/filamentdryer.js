$(function () {
    function DryerControlViewModel(parameters) {
        const self = this;
        self.onBeforeBinding = function () {
            self.loadSettings();
            self.fetchState();
            setInterval(self.fetchState, 5000);
            // setInterval(self.fetchHistory, 10000);
        };


        self.fan_pin = ko.observable();
        self.heater_pin = ko.observable();
        self.target_temp = ko.observable();
        self.tolerance = ko.observable();
        self.saveStatus = ko.observable(false);

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

        // self.toggleSystem = function () {
        //     OctoPrint.simpleApiCommand("dryer_control", "toggle");
        // };
        //
        // self.fetchState = function () {
        //     $.get("http://10.0.0.26:8000/system", function (data) {
        //         self.systemOn(data.system_on);
        //     });
        // };
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
