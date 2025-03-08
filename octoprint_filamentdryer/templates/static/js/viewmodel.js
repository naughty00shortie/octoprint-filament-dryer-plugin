$(function() {
    function FilamentDryerViewModel(parameters) {
        var self = this;
        self.settings = parameters[0];
    }

    OCTOPRINT_VIEWMODELS.push({
        construct: FilamentDryerViewModel,
        dependencies: ["settingsViewModel"],
        elements: ["#filamentdryer-navbar"]
    });
});
