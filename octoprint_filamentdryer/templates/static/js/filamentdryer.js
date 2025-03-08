$(function() {
    function FilamentDryerViewModel(parameters) {
        var self = this;
        self.settingsViewModel = parameters[0];

        self.dryerOn = ko.observable(false);

        self.toggleDryer = function() {
            var command = self.dryerOn() ? "stop" : "start";

            $.ajax({
                url: API_BASEURL + "plugin/filamentdryer",
                type: "POST",
                data: JSON.stringify({ command: command }),
                contentType: "application/json",
                success: function(response) {
                    console.log("Dryer toggled:", response);
                    self.dryerOn(!self.dryerOn());
                },
                error: function(xhr, status, error) {
                    console.error("Failed to toggle the dryer:", error);
                    alert("Failed to toggle the dryer. Check OctoPrint logs.");
                }
            });
        };
    }

    OCTOPRINT_VIEWMODELS.push({
        construct: FilamentDryerViewModel,
        dependencies: ["settingsViewModel"],
        elements: ["#filamentdryer-navbar"]
    });
});
