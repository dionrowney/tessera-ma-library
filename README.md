# Music Assistant library

Keys for your Music Assistant library. Each key opens one list, playlists, artists, radio stations or podcasts, over the
whole screen, a page at a time. Tap a name to play it on the key's Music Assistant player.

It asks Home Assistant for each page (`music_assistant.get_library`) through the Tessera app, so nothing has to be added
to Home Assistant: no helpers, no scripts, no automations.

## Set up

1. Make sure the Music Assistant integration is set up in Home Assistant and has at least one player.
2. Find the integration's config entry id: in Home Assistant open **Developer Tools > Template** and enter
   `{{ config_entry_id('media_player.your_player') }}` with one of your Music Assistant players. The result looks like
   `01JABCDEFGHJKMNPQRSTVWXYZ0`. Copy it.
3. Add the plugin to a screen in Tessera and paste the id when it asks. The screen builds once.
4. In Layout, place the **Music list** tile from the library's Plugins group, as small as one cell. Place one for every
   list you want.
5. In the inspector, choose the Music Assistant player that plays and the **List** the key opens. Name the tile to title
   the key; without a name it says what the list is.

## Good to know

- A list opens on your favorites in Music Assistant, every time. The **Favorites** key at the top turns the filter off to
  show everything, and on again. It is lit while the filter is on.
- The list shows as many rows as fit the screen, up to twelve a page, sorted by name. The arrows at the bottom change
  the page, and the round key or a tap beside the list closes it.
- Each page is asked again when you open the list or change the page, so what you see is what the library has now.
- Only the names and uris of a page reach the screen. Music Assistant finds and plays the item itself.
- The config entry id changes when the Music Assistant integration is removed and added again. Paste the new one in the
  plugin's details on the screen's Plugins tab and press **Save and build**. Each screen keeps its own.
