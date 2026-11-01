# Music Assistant library

A tile with your Music Assistant playlists, artists and radio stations, a page at a time. Tap a tab to switch lists,
the arrows to page, and a name to play it on the tile's Music Assistant player.

It asks Home Assistant for each page (`music_assistant.get_library`) through the Tessera app, so nothing has to be added
to Home Assistant: no helpers, no scripts, no automations.

## Set up

1. Make sure the Music Assistant integration is set up in Home Assistant and has at least one player.
2. Find the integration's config entry id: in Home Assistant open **Developer Tools > Template** and enter
   `{{ config_entry_id('media_player.your_player') }}` with one of your Music Assistant players. The result looks like
   `01JABCDEFGHJKMNPQRSTVWXYZ0`. Copy it.
3. Add the plugin to a screen in Tessera. The screen builds once.
4. In Layout, place the **Music library** tile from the library's Plugins group. It is at least 1 column wide and 2 rows high.
5. In the inspector, choose the Music Assistant player that plays, paste the config entry id, and choose the list it
   opens on.

## Good to know

- The tile shows as many rows as fit its size, up to twelve a page, sorted by name. On a tile one column wide the
  three tabs become one: tap it to go from playlists to artists to radio.
- Each page is asked again when you switch tabs or page, so what you see is what the library has now.
- Only the names and uris of a page reach the screen. Music Assistant finds and plays the item itself.
- The config entry id changes when the Music Assistant integration is removed and added again: paste the new one.
