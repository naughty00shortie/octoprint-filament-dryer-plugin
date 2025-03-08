$(function() {
    function FilamentDryerViewModel(parameters) {
        var self = this;
        self.settingsViewModel = parameters[0];

        self.dryerOn = ko.observable(false);

        self.toggleDryer = function() {
            if (self.dryerOn()) {
                $.ajax({
                    url: API_BASEURL + "plugin/filamentdryer",
                    type: "POST",
                    data: JSON.stringify({ command: "stop" }),
                    contentType: "application/json",
                    success: function() {
                        self.dryerOn(false);
                    },
                    error: function() {
                        alert('Failed to turn off the dryer.');
                    }
                });
            } else {
                $.ajax({
                    url: API_BASEURL + "plugin/filamentdryer",
                    type: "POST",
                    data: JSON.stringify({ command: "start" }),
                    contentType: "application/json",
                    success: function() {
                        self.dryerOn(true);
                    },
                    error: function() {
                        alert('Failed to turn on the dryer.');
                    }
                });
            }
        };
    }

    OCTOPRINT_VIEWMODELS.push({
        construct: FilamentDryerViewModel,
        dependencies: ["settingsViewModel"],
        elements: ["#filamentdryer-navbar"]
    });
});
