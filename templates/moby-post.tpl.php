<?php
/**
 * @file
 * Shows the content of a post: its photos and videos, description, and
 * location, in the order of the content type's display settings.
 *
 * The theme's node template still prints the post's title, date, and links
 * around this. To change it, copy this file into your theme.
 *
 * Available variables:
 * - $media: The rendered photos and videos.
 * - $description: The rendered description.
 * - $location: The rendered location name.
 * - $coordinates: The rendered map coordinates. Only provided by Geofield, and
 *   empty without it.
 * - $location_icon: A map pin icon, labeled "Location" for screen readers.
 * - $order: The parts in display order: "media", "description", and
 *   "location" (and "date", which the node template prints). Apps show posts
 *   in the same order.
 * - $node: The post.
 * - $view_mode: The view mode the post is shown in, such as "full".
 * - $classes: An array of classes for the wrapper.
 * - $attributes: An array of other attributes for the wrapper.
 *
 * @see template_preprocess_moby_post()
 * @see moby_post_order()
 */
?>
<div class="<?php print implode(' ', $classes); ?>"<?php print backdrop_attributes($attributes); ?>>
  <?php foreach ($order as $part): ?>
    <?php if ($part === 'media' && $media): ?>
      <div class="moby-post-media"><?php print $media; ?></div>

    <?php elseif ($part === 'description' && $description): ?>
      <div class="moby-post-description"><?php print $description; ?></div>

    <?php elseif ($part === 'location' && ($location || $coordinates)): ?>
      <div class="moby-post-location">
        <?php print $location_icon; ?>
        <div class="moby-post-location-text">
          <?php print $location; ?>
          <?php if ($coordinates): ?>
            <div class="moby-post-coordinates"><?php print $coordinates; ?></div>
          <?php endif; ?>
        </div>
      </div>
    <?php endif; ?>
  <?php endforeach; ?>
</div>
