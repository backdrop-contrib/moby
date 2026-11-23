Moby
====

Moby lets the Moby microblogging apps for iOS and Android post to a Backdrop
site. It provides a REST API for creating posts with a description, a date, a
location, tags, and photos or videos, saved into the content type you choose.


Requirements
------------

This module requires only Backdrop core: the Node, File, and Image modules.

To store location coordinates, the
[Geofield](https://backdropcms.org/project/geofield) module is also needed.
Without it, posts can only have a location name.


Installation
------------

- Install this module using the official Backdrop CMS instructions at
  https://docs.backdropcms.org/documentation/extend-with-modules.

- Enabling the module shows a link to a setup wizard, which is also available
  from the settings page. It walks through the steps below, and can create a
  new "Mobile Post" content type with all the fields it needs.

- Grant **Post via Moby** to the roles that may post from the app, along with
  permission to create, edit, and delete their own content of the content
  type. Also grant **View own unpublished content**, so users can read back
  drafts they post.

- Visit the configuration page under Administration > Configuration > Web
  services > Moby (admin/config/services/moby). Select a content type and
  save. Then map the description, date, location, and media to that content
  type's fields.

- Optionally, to store location coordinates, enable the Geofield module, add
  a Geofield to the content type, and select it as "Location coordinates".
  Without Geofield, this setting is hidden, and the app only offers a location
  name.

- Optionally, to store a thumbnail for each video, add an image field to the
  Video file type (Structure > File types) and select it as the "Video
  thumbnail field".

- Tags from the app, and #hashtags in descriptions, are saved in a "Tag
  vocabulary", which defaults to "Tags" if the site has it. Choose another, or
  none, in the wizard or on the settings page. A tags field is added to the
  content type if it has none.

- Optionally, choose the "Thumbnail image style" that apps show in place of
  full-size photos and video thumbnails. It defaults to `large`.


Documentation
-------------

### Showing posts

Posts are shown with `templates/moby-post.tpl.php`, which the theme's node
template prints in place of the post's fields. It shows the photos and videos,
then the description, then the location with a map pin icon. Exact coordinates
are shown only when they're stored in a Geofield, as
"39.96124° N, 105.51306° W". To change the layout, copy the template into your
theme.

The **Moby Carousel** formatter, for file and image fields, shows photos and
videos one at a time. The setup wizard uses it for the media field. It:

- Is written for this module, with no library (see "Carousel libraries"
  below).
- Follows the
  [WAI-ARIA carousel pattern](https://www.w3.org/WAI/ARIA/apg/patterns/carousel/):
  labeled previous and next buttons (44px), a dot for each slide, slide
  positions announced to screen readers, slides out of view kept from being
  focused, and no automatic rotation.
- Uses the browser's own sideways scrolling with snap points, so swiping feels
  native on touch screens, and the slides can still be scrolled without
  JavaScript.
- Respects reduced motion and Windows high contrast mode, and pauses videos
  that are moved away from.
- Gives every slide the shape of the tallest photo or video, within limits, so
  the page doesn't jump. A single item is shown on its own, without controls.

Photos and videos never grow wider than the content area. Themes can restyle
the carousel with the CSS custom properties listed in `css/moby.carousel.css`.

### Carousel libraries

These were considered before writing the carousel from scratch:

- [Splide](https://splidejs.com/) (MIT): the best accessibility of the
  libraries, with touch support, but about 30 KB, and it re-implements
  swiping in JavaScript.
- [Swiper](https://swiperjs.com/) (MIT): full featured and widely used, but
  large (40 KB or more), and its accessibility module needs care to set up.
- [Embla Carousel](https://www.embla-carousel.com/) (MIT): small and
  well-built, but it provides no buttons, dots, or ARIA; those would have to be
  written anyway.
- [Glide.js](https://glidejs.com/) (MIT): small, but weak on accessibility and
  little maintained.
- [Flickity](https://flickity.metafizzy.co/): needs a commercial license for
  non-GPL use, and its accessibility is limited.

Native scroll snapping gives touch behavior as good as these libraries, so the
carousel only needed a small script for the buttons, dots, and ARIA.

### API

The REST API is described in [`../moby_docs/API.md`](../moby_docs/API.md),
with the rest of the Moby documentation.

### Extending

Other modules can provide authentication mechanisms with
`hook_moby_auth_info()`. See `moby.api.php` and
`includes/moby_auth.class.inc`.

### Status

Under development. Working so far: the menu routes, JSON delivery, error
handling, the settings form, token authentication, media uploads, creating
posts, and listing, viewing, updating, and deleting posts.


Issues
------

Bugs and Feature Requests should be reported in the Issue Queue:
https://github.com/backdrop-contrib/moby/issues.


Current Maintainers
-------------------

- [Nate Lampton](https://github.com/quicksketch)


Credits
-------

- Written for Backdrop CMS by [Nate Lampton](https://github.com/quicksketch).


License
-------

This project is GPL v2 software.
See the LICENSE.txt file in this directory for complete text.
