$(function() {
    function FilamentDryerSettingsViewModel(parameters) {
        var self = this;
        self.settings = parameters[0];

        self.target_temp = ko.observable();
        self.tolerance = ko.observable();
        self.fan_on_cmd = ko.observable();
        self.fan_off_cmd = ko.observable();
        self.element_on_cmd = ko.observable();
        self.element_off_cmd = ko.observable();

        self.onSettingsShown = function() {
            self.target_temp(self.settings.settings.plugins.filamentdryer.target_temp());
            self.tolerance(self.settings.settings.plugins.filamentdryer.tolerance());
            self.fan_on_cmd(self.settings.settings.plugins.filamentdryer.fan_on_cmd());
            self.fan_off_cmd(self.settings.settings.plugins.filamentdryer.fan_off_cmd());
            self.element_on_cmd(self.settings.settings.plugins.filamentdryer.element_on_cmd());
            self.element_off_cmd(self.settings.settings.plugins.filamentdryer.element_off_cmd());
        };

        self.onSettingsBeforeSave = function() {
            self.settings.settings.plugins.filamentdryer.target_temp(self.target_temp());
            self.settings.settings.plugins.filamentdryer.tolerance(self.tolerance());
            self.settings.settings.plugins.filamentdryer.fan_on_cmd(self.fan_on_cmd());
            self.settings.settings.plugins.filamentdryer.fan_off_cmd(self.fan_off_cmd());
            self.settings.settings.plugins.filamentdryer.element_on_cmd(self.element_on_cmd());
            self.settings.settings.plugins.filamentdryer.element_off_cmd(self.element_off_cmd());
        };
    }

    OCTOPRINT_VIEWMODELS.push({
        construct: FilamentDryerSettingsViewModel,
        dependencies: ["settingsViewModel"],
        elements: ["#settings_plugin_filamentdryer"]
    });
});
