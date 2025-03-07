$(function() {
    function updateSettings() {
        var targetTemperature = $('#targetTemperature').val();
        var tolerance = $('#tolerance').val();

        // Make sure the values are valid numbers
        if (!isNaN(targetTemperature) && !isNaN(tolerance)) {
            // Send the updated settings to the plugin
            OctoPrint.plugins.temperature_control.setSettings(targetTemperature, tolerance);
        } else {
            alert("Invalid input. Please enter valid numeric values.");
        }
    }

    // When the settings are saved, apply the values from the input fields
    function loadSettings(settings) {
        $('#targetTemperature').val(settings.target_temperature);
        $('#tolerance').val(settings.tolerance);
    }

    // Bind the save button click event
    $('#saveSettings').click(updateSettings);

    // Listen to the settings being loaded into the UI
    OctoPrint.settings.onSettingsLoaded(function() {
        var settings = OctoPrint.settings.plugins.temperature_control;
        loadSettings(settings);
    });
});
