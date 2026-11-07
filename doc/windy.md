[← Back to the overview](../README.md)

# Windy App

## 📊 Available Widgets / Sensor Modes

The application dynamically renders weather widgets based on the data received from the backend.
Depending on your configuration and hardware setup, two distinct sensor outputs are generated:

* **Windy App by Spot ID**
  This widget displays the wind and weather forecast for your fixed, pre-configured favorite windsurfing location.
  It uses the `Windy Spot ID` defined in your settings and will load this specific spot consistently, making it ideal for checking your home spot.

* **Windy App by Spot GPS**
  This widget automatically adapts to your current physical position. If a GPS receiver is connected and active (e.g., while traveling in your camper),
  the system uses your live coordinates to automatically find and display the nearest windsurfing spot registered in the Windy.app database.

## 🛠️ How to Get Your Windy.app API Key

The weather widgets require a free Application ID from **Windy.app**. You do not need to register a developer account; the ID is automatically generated directly through their website.

### Method 1: Using Any Windsurf/Weather Spot (Fastest)
1. Go to any weather spot page on [Windy.app](https://windy.app) (e.g., the [Gialova Navarino Spot](https://windy.app)).
2. Scroll down the page until you find the section titled **"Live wind map. Install on your site."** or **"Weather widget"**.
3. Look at the generated HTML code snippet. It will look like this:
   ```html
   <div data-windywidget="forecast" data-thememode="white" data-spotid="444217" data-appid="e6865e0c4a1713e956bd11fc0b5fec63"></div>
   ```
### Method 2: Via the Official Widget Generator
1. Open the [Windy.app Widget Generator](https://windy.app).
2. Choose your preferred widget layout.
3. Click the button to generate the HTML code.
4. Extract the hex string found inside the `data-appid` field from the generated code block.

Copy this number and paste it into your configuration (Setup -> WEB Interface -> Windy App Spot ID).
> ⚠️ **Note on the `widgets_` prefix:** If this application automatically prepends `widgets_` to the key in the frontend, you only need to provide the raw 32-character key in your configuration file. Do not include the `widgets_` prefix yourself.

## How to Find a Specific Windsurf Spot ID
If you want the widget to always load a specific favorite windsurfing location by default, you can provide its unique Spot ID:

1. Open your browser and go to [Windy.app](https://windy.app).
2. Use the search bar to find your favorite windsurfing spot (e.g., search for "Gialova Navarino").
3. Look at the URL in your browser's address bar. It will look like this:
   `https://windy.app`
4. The number right after `/spot/` is your unique Spot ID. In this example, the ID is **444217**.

Copy this number and paste it into your configuration (Setup -> WEB Interface -> Windy App Spot ID).
Every spot ID stored there is remembered and offered as suggestion of the input field with the spot's
name (the setup page asks Windy for it), so a previously used spot can be picked by name; the trash
button forgets the entered value. Windy offers no lookup of a spot by name, only the ID is accepted.

[← Back to the overview](../README.md)
