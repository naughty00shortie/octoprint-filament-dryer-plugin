$(function () {
    function DryerControlViewModel(parameters) {
        const self = this;
        self.systemOn = ko.observable(false);
        self.tempData = {
            actual_temp: [],
            target_temp: [],
            humidity: []
        };

        self.toggleSystem = function () {
            OctoPrint.simpleApiCommand("dryer_control", "toggle");
        };

        self.fetchState = function () {
            $.get("http://localhost:8000/system", function (data) {
                self.systemOn(data.system_on);
            });
        };

        self.fetchHistory = function () {
            $.get("http://localhost:8000/history", function (data) {
                self.tempData.actual_temp = data.map(d => [d.timestamp * 1000, d.actual_temp]);
                self.tempData.target_temp = data.map(d => [d.timestamp * 1000, d.target_temp]);
                self.tempData.humidity = data.map(d => [d.timestamp * 1000, d.humidity]);

                // Add to temp chart (if possible)
                if (typeof tempGraph !== 'undefined') {
                    tempGraph.plot.getOptions().series.push(
                        {label: "Dryer Temp", color: "#ffa500", data: self.tempData.actual_temp},
                        {label: "Dryer Target", color: "#00ff00", data: self.tempData.target_temp},
                        {label: "Dryer Humidity", color: "#3399ff", data: self.tempData.humidity}
                    );
                    tempGraph.plot.setupGrid();
                    tempGraph.plot.draw();
                }
            });
        };

        self.onBeforeBinding = function () {
            self.fetchState();
            setInterval(self.fetchState, 5000);
            setInterval(self.fetchHistory, 10000);
        };
    }

    OCTOPRINT_VIEWMODELS.push({
        construct: DryerControlViewModel,
        dependencies: ["settingsViewModel", "temperatureViewModel"],
        elements: ["#navbar_plugin_filamentdryer", "#tab_plugin_filamentdryer"]
    });
});
