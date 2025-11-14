# Audio Wave Visualizer for OBS Studio – User Docs

This archive contains a **user-friendly documentation page** for the
**Audio Wave (Simple)** visualizer source plugin for OBS Studio.

## Structure

- `docs/index.html` – main documentation page (client-oriented).
- `docs/assets/css/style.css` – styling for the docs page.
- `docs/assets/img/` – folder for screenshots referenced by the docs.

Expected screenshot filenames (you can replace the placeholders with your real images):

- `audio-wave-sources-list.png` – First screenshot with the Sources `+` button and arrows to the new source item.
- `audio-wave-blank-setup.png` – Properties window with the blank / default setup.
- `audio-wave-custom-setup.png` – Properties window with some customized settings applied.

## How to use

1. Replace the placeholder images in `docs/assets/img/` with your real screenshots,
   keeping the same filenames.
2. Adjust any text in `docs/index.html` if you want to change wording, the plugin name,
   or repository URL.
3. Upload the `docs/` folder to your project (e.g. as a GitHub Pages site, or as part of your plugin’s website).

### Support & GitHub links

By default, the docs include:

- Ko-fi: `https://ko-fi.com/mmltech`
- PayPal: `https://paypal.me/mmltools`
- GitHub repo (example): `https://github.com/mmlTools/obs-audio-wave-visualizer`

If your repository name is different, edit the link in `docs/index.html`:

```html
<a class="btn btn-github" href="https://github.com/mmlTools/obs-audio-wave-visualizer" target="_blank" rel="noopener">
  &lt;/&gt; Star &amp; Fork on GitHub
</a>
```

Change the URL to the actual repository for this plugin.

## Deploying as GitHub Pages (optional)

If this docs folder lives in your plugin repository:

1. Put the `docs/` folder at the root of the repo.
2. In the GitHub repo settings, enable GitHub Pages and choose the `docs/` folder as the source.
3. GitHub will serve `docs/index.html` as a small documentation website.

Your users can then access a nice, client-oriented guide without needing to read code or technical notes.
