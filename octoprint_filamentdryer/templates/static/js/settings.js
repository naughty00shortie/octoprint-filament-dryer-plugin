$(function() {
    function FilamentDryerViewModel(parameters) {
        var self = this;

        self.settings = parameters[0];
        self.isSystemOn = ko.observable(false);

        self.target_temp = ko.observable();
        self.tolerance = ko.observable();
        self.fan_on_cmd = ko.observable();
        self.fan_off_cmd = ko.observable();
        self.element_on_cmd = ko.observable();
        self.element_off_cmd = ko.observable();

        self.onBeforeBinding = function() {
            self.target_temp(self.settings.settings.plugins.filamentdryer.target_temp());
            self.tolerance(self.settings.settings.plugins.filamentdryer.tolerance());
            self.fan_on_cmd(self.settings.settings.plugins.filamentdryer.fan_on_cmd());
            self.fan_off_cmd(self.settings.settings.plugins.filamentdryer.fan_off_cmd());
            self.element_on_cmd(self.settings.settings.plugins.filamentdryer.element_on_cmd());
            self.element_off_cmd(self.settings.settings.plugins.filamentdryer.element_off_cmd());
        };

        self.saveSettings = function() {
            self.settings.settings.plugins.filamentdryer.target_temp(self.target_temp());
            self.settings.settings.plugins.filamentdryer.tolerance(self.tolerance());
            self.settings.settings.plugins.filamentdryer.fan_on_cmd(self.fan_on_cmd());
            self.settings.settings.plugins.filamentdryer.fan_off_cmd(self.fan_off_cmd());
            self.settings.settings.plugins.filamentdryer.element_on_cmd(self.element_on_cmd());
            self.settings.settings.plugins.filamentdryer.element_off_cmd(self.element_off_cmd());

            self.settings.saveData();
        };

        self.toggleSystem = function() {
            self.isSystemOn(!self.isSystemOn());

            $.ajax({
                url: "/api/plugin/filamentdryer",
                type: "POST",
                contentType: "application/json",
                data: JSON.stringify({ action: self.isSystemOn() ? "start" : "stop" }),
                success: function(response) {
                    console.log("System " + (self.isSystemOn() ? "ON" : "OFF"));
                }
            });
        };
    }

    $(document).ready(function() {
        $("#navbar").append(
            '<li id="filamentdryer-button">' +
            '<a href="#" id="filamentdryer-toggle">' +
            '<i class="fas fa-fire"></i>' +
            '</a>' +
            '</li>'
        );

        $("#filamentdryer-toggle").click(function() {
            var viewModel = ko.dataFor(this);
            viewModel.toggleSystem();
        });
    });

    OCTOPRINT_VIEWMODELS.push({
        construct: FilamentDryerViewModel,
        dependencies: ["settingsViewModel"],
        elements: ["#settings_plugin_filamentdryer"]
    });
});
