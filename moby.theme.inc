<?php
/**
 * @file
 * Theme functions for showing posts and the Moby Carousel field formatter.
 */

/**
 * Prepares variables for moby-post.tpl.php.
 *
 * @param array $variables
 *   An associative array containing:
 *   - node: The post.
 *   - view_mode: The view mode the post is shown in.
 *   - description, media, location, coordinates: The render arrays of the
 *     fields mapped to each, or NULL where a field is not mapped or is hidden
 *     in the view mode.
 */
function template_preprocess_moby_post(&$variables) {
  $variables['classes'][] = 'moby-post--' . backdrop_html_class($variables['view_mode']);
  foreach (array('description', 'media', 'location') as $key) {
    $variables[$key] = $variables[$key] ? render($variables[$key]) : '';
  }
  // Exact coordinates are only shown when they are stored and displayed by
  // Geofield. Without it, posts show only their location's name.
  $coordinates = '';
  if ($variables['coordinates'] && module_exists('geofield')) {
    $coordinates = moby_post_format_coordinates($variables['coordinates']);
  }
  $variables['coordinates'] = $coordinates;
  $variables['location_icon'] = icon('map-pin-fill', array(
    'alt' => t('Location'),
    'attributes' => array('class' => array('moby-post-location-icon')),
  ));
}

/**
 * Formats a Geofield's point for reading, such as "39.96124° N, 105.51306° W".
 *
 * @param array $element
 *   The field's render array.
 *
 * @return string
 *   The formatted coordinates, or the field rendered by its formatter if they
 *   aren't a point.
 */
function moby_post_format_coordinates(array $element) {
  $item = isset($element['#items']) ? reset($element['#items']) : FALSE;
  if (!$item || !isset($item['lat']) || !isset($item['lon']) || (isset($item['geo_type']) && $item['geo_type'] !== 'point')) {
    return render($element);
  }
  // Five decimal places is about a meter.
  $latitude = (float) $item['lat'];
  $longitude = (float) $item['lon'];
  return t('@latitude° @ns, @longitude° @ew', array(
    '@latitude' => number_format(abs($latitude), 5),
    '@ns' => $latitude < 0 ? t('S') : t('N'),
    '@longitude' => number_format(abs($longitude), 5),
    '@ew' => $longitude < 0 ? t('W') : t('E'),
  ));
}

/**
 * Returns the media items of a file or image field, ready for the carousel.
 *
 * @param array $items
 *   The field's items.
 * @param array $settings
 *   The formatter's settings.
 *
 * @return array
 *   A list of arrays, each with the following keys:
 *   - type: "image", "video", or "file" for files that cannot be shown.
 *   - markup: The HTML of the photo, video, or link to the file.
 *   - width, height: The size it is shown at, or NULL when unknown.
 */
function moby_carousel_items(array $items, array $settings) {
  $fids = array();
  foreach ($items as $item) {
    $fids[] = $item['fid'];
  }
  $files = file_load_multiple($fids);
  $thumbnail_field = moby_video_thumbnail_field();
  $style = !empty($settings['image_style']) ? image_style_load($settings['image_style']) : FALSE;

  $result = array();
  foreach (array_values($items) as $index => $item) {
    if (empty($files[$item['fid']])) {
      continue;
    }
    $file = $files[$item['fid']];
    // File fields keep alt text in the description; image fields in "alt".
    $alt = '';
    if (isset($item['alt']) && $item['alt'] !== '') {
      $alt = $item['alt'];
    }
    elseif (isset($item['description'])) {
      $alt = $item['description'];
    }
    $media_type = strpos($file->filemime, 'video/') === 0 ? 'video' : (moby_carousel_image_supported($file->filemime) ? 'image' : 'file');

    $slide = array('type' => $media_type, 'markup' => '', 'width' => NULL, 'height' => NULL);
    if ($media_type === 'image') {
      $dimensions = moby_carousel_image_dimensions($file);
      if ($style && $dimensions['width']) {
        image_style_transform_dimensions($style['name'], $dimensions);
      }
      $slide['width'] = $dimensions['width'];
      $slide['height'] = $dimensions['height'];
      $image = array(
        'uri' => $file->uri,
        'width' => $dimensions['width'],
        'height' => $dimensions['height'],
        'alt' => $alt,
        // The first photo is likely in view as the page loads.
        'attributes' => $index ? array('loading' => 'lazy', 'decoding' => 'async') : array(),
      );
      if ($style) {
        $slide['markup'] = theme('image_style', $image + array('style_name' => $style['name']));
      }
      else {
        $slide['markup'] = theme('image', $image + array('path' => $file->uri));
      }
    }
    elseif ($media_type === 'video') {
      $url = file_create_url($file->uri);
      $attributes = array(
        'controls' => 'controls',
        'preload' => 'metadata',
        'playsinline' => 'playsinline',
      );
      if ($thumbnail_field && ($thumbnails = field_get_items('file', $file, $thumbnail_field['field']['field_name']))) {
        $thumbnail = reset($thumbnails);
        $attributes['poster'] = file_create_url($thumbnail['uri']);
        if (!empty($thumbnail['width']) && !empty($thumbnail['height'])) {
          $slide['width'] = $attributes['width'] = (int) $thumbnail['width'];
          $slide['height'] = $attributes['height'] = (int) $thumbnail['height'];
        }
      }
      if ($alt !== '') {
        $attributes['aria-label'] = $alt;
      }
      $slide['markup'] = '<video' . backdrop_attributes($attributes) . '>'
        . '<source src="' . check_plain($url) . '" type="' . check_plain($file->filemime) . '" />'
        . l(t('Download the video'), $url)
        . '</video>';
    }
    else {
      $slide['markup'] = theme('file_link', array('file' => $file));
    }
    $result[] = $slide;
  }
  return $result;
}

/**
 * Checks whether browsers can show images of a MIME type.
 */
function moby_carousel_image_supported($mime_type) {
  return in_array($mime_type, array(
    'image/jpeg',
    'image/png',
    'image/gif',
    'image/webp',
    'image/avif',
    'image/svg+xml',
  ));
}

/**
 * Returns the width and height of an image file, if they can be found.
 */
function moby_carousel_image_dimensions($file) {
  if (!empty($file->metadata['width']) && !empty($file->metadata['height'])) {
    return array('width' => (int) $file->metadata['width'], 'height' => (int) $file->metadata['height']);
  }
  if ($info = image_get_info($file->uri)) {
    return array('width' => (int) $info['width'], 'height' => (int) $info['height']);
  }
  return array('width' => NULL, 'height' => NULL);
}

/**
 * Prepares variables for moby-carousel.tpl.php.
 *
 * @param array $variables
 *   An associative array containing:
 *   - items: The media items, as returned by moby_carousel_items().
 *   - label: The carousel's accessible name.
 */
function template_preprocess_moby_carousel(&$variables) {
  $items = $variables['items'];
  $count = count($items);
  $variables['id'] = backdrop_html_id('moby-carousel');
  $variables['count'] = $count;
  $variables['single'] = $count < 2;
  $variables['label'] = $variables['label'] ? $variables['label'] : t('Photos and videos');
  if ($variables['single']) {
    $variables['classes'][] = 'moby-carousel--single';
  }

  // Every slide gets the shape of the tallest item, within limits, so the
  // carousel keeps one height and the page doesn't jump as it moves.
  $ratio = 0;
  foreach ($items as $item) {
    if ($item['width'] && $item['height']) {
      $ratio = max($ratio, $item['height'] / $item['width']);
    }
  }
  $ratio = $ratio ? min(max($ratio, 9 / 16), 5 / 4) : 3 / 4;
  $variables['attributes']['style'] = '--moby-carousel-ratio: 1000 / ' . round(1000 * $ratio) . ';';
  if (!$variables['single']) {
    $variables['attributes']['aria-roledescription'] = t('carousel');
    $variables['attributes']['aria-label'] = $variables['label'];
  }

  foreach ($items as $index => &$item) {
    $item['id'] = $variables['id'] . '-slide-' . ($index + 1);
    $item['label'] = t('@index of @count', array('@index' => $index + 1, '@count' => $count));
  }
  $variables['items'] = $items;
  // A dot for each slide, unless there are too many to tell apart.
  $variables['show_dots'] = $count > 1 && $count <= 12;
  $variables['previous_icon'] = icon('caret-left');
  $variables['next_icon'] = icon('caret-right');
}
