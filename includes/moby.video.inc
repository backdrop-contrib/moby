<?php
/**
 * @file
 * Reads metadata from MP4 and QuickTime (MOV) video files.
 *
 * Both formats are made of nested "boxes" (ISO/IEC 14496-12). The movie
 * header ("moov/mvhd") holds the duration, and each track header
 * ("moov/trak/tkhd") holds that track's dimensions and rotation. Boxes are
 * read by seeking, so large videos are never loaded into memory.
 */

/**
 * The maximum number of boxes read at each level, to bound malformed files.
 */
define('MOBY_VIDEO_MAX_BOXES', 1000);

/**
 * Gets the dimensions and duration of an MP4 or QuickTime video.
 *
 * @param string $uri
 *   The URI or path of the video file.
 *
 * @return array|false
 *   An array containing:
 *   - width: The display width in pixels, or NULL if unknown.
 *   - height: The display height in pixels, or NULL if unknown.
 *   - duration: The length in seconds, or NULL if unknown.
 *   Width and height account for rotation, so a portrait video recorded on a
 *   phone is taller than it is wide. FALSE is returned if the file is not an
 *   MP4 or QuickTime video.
 *
 * @see image_get_info()
 */
function moby_video_get_info($uri) {
  $handle = @fopen($uri, 'rb');
  if (!$handle) {
    return FALSE;
  }
  $stat = fstat($handle);

  $info = FALSE;
  foreach (_moby_video_boxes($handle, 0, $stat['size']) as $box) {
    if ($box['type'] === 'moov') {
      $info = _moby_video_parse_moov($handle, $box);
      break;
    }
  }

  fclose($handle);
  return $info;
}

/**
 * Parses the movie ("moov") box.
 */
function _moby_video_parse_moov($handle, array $moov) {
  $info = array(
    'width' => NULL,
    'height' => NULL,
    'duration' => NULL,
  );

  foreach (_moby_video_boxes($handle, $moov['start'], $moov['end']) as $box) {
    if ($box['type'] === 'mvhd') {
      $info['duration'] = _moby_video_parse_mvhd(_moby_video_read($handle, $box, 32));
    }
    elseif ($box['type'] === 'trak' && !isset($info['width'])) {
      if ($dimensions = _moby_video_parse_trak($handle, $box)) {
        list($info['width'], $info['height']) = $dimensions;
      }
    }
  }

  return $info;
}

/**
 * Parses the duration in seconds from a movie header ("mvhd") box.
 */
function _moby_video_parse_mvhd($data) {
  if (strlen($data) < 32) {
    return NULL;
  }
  // Version 1 headers use 64-bit times and durations.
  if (ord($data[0]) === 1) {
    $values = unpack('Ntimescale/Jduration', substr($data, 20, 12));
  }
  else {
    $values = unpack('Ntimescale/Nduration', substr($data, 12, 8));
  }
  // A duration of all ones means it is unknown, as in fragmented files.
  if (!$values['timescale'] || !$values['duration'] || $values['duration'] == 0xFFFFFFFF) {
    return NULL;
  }
  return round($values['duration'] / $values['timescale'], 3);
}

/**
 * Parses the display dimensions of a track ("trak") box, if it is video.
 *
 * @return array|false
 *   The width and height, or FALSE if this is not a video track.
 */
function _moby_video_parse_trak($handle, array $trak) {
  $tkhd = NULL;
  $handler = NULL;
  foreach (_moby_video_boxes($handle, $trak['start'], $trak['end']) as $box) {
    if ($box['type'] === 'tkhd') {
      $tkhd = _moby_video_read($handle, $box, 96);
    }
    elseif ($box['type'] === 'mdia') {
      foreach (_moby_video_boxes($handle, $box['start'], $box['end']) as $mdia_box) {
        if ($mdia_box['type'] === 'hdlr') {
          $handler = substr(_moby_video_read($handle, $mdia_box, 12), 8, 4);
        }
      }
    }
  }
  if ($handler !== 'vide' || !isset($tkhd)) {
    return FALSE;
  }

  // Version 1 headers have 64-bit times and durations, moving later fields.
  $offset = (ord($tkhd[0]) === 1) ? 52 : 40;
  if (strlen($tkhd) < $offset + 44) {
    return FALSE;
  }
  // The transformation matrix holds 16.16 fixed-point values a, b, u, c, d,
  // v, x, y, w, followed by the 16.16 fixed-point width and height.
  $values = unpack('Na/Nb/Nu/Nc/Nd/Nv/Nx/Ny/Nw/Nwidth/Nheight', substr($tkhd, $offset, 44));
  $width = (int) round($values['width'] / 65536);
  $height = (int) round($values['height'] / 65536);
  if (!$width || !$height) {
    return FALSE;
  }
  // When a and d are zero, the video is rotated by 90 or 270 degrees, so it
  // is displayed with its width and height swapped.
  if ($values['a'] == 0 && $values['d'] == 0) {
    return array($height, $width);
  }
  return array($width, $height);
}

/**
 * Lists the boxes within a byte range of a file.
 *
 * @param resource $handle
 *   The open file.
 * @param int $start
 *   The offset of the first box.
 * @param int $end
 *   The offset just past the last box.
 *
 * @return array
 *   A list of boxes, each containing its four-character "type" and the
 *   "start" and "end" offsets of its contents.
 */
function _moby_video_boxes($handle, $start, $end) {
  $boxes = array();
  $position = $start;
  while ($position + 8 <= $end && count($boxes) < MOBY_VIDEO_MAX_BOXES) {
    fseek($handle, $position);
    $header = fread($handle, 8);
    if (strlen($header) < 8) {
      break;
    }
    $values = unpack('Nsize/a4type', $header);
    $size = $values['size'];
    $header_size = 8;
    if ($size === 1) {
      // The size is too large for 32 bits, and follows as a 64-bit value.
      $large_size = fread($handle, 8);
      if (strlen($large_size) < 8) {
        break;
      }
      $size = unpack('J', $large_size)[1];
      $header_size = 16;
    }
    elseif ($size === 0) {
      // The box extends to the end of its container.
      $size = $end - $position;
    }
    if ($size < $header_size || $position + $size > $end) {
      break;
    }
    $boxes[] = array(
      'type' => $values['type'],
      'start' => $position + $header_size,
      'end' => $position + $size,
    );
    $position += $size;
  }
  return $boxes;
}

/**
 * Reads the start of a box's contents.
 *
 * @param resource $handle
 *   The open file.
 * @param array $box
 *   A box from _moby_video_boxes().
 * @param int $length
 *   The maximum number of bytes to read.
 *
 * @return string
 *   The bytes read, which may be shorter than $length.
 */
function _moby_video_read($handle, array $box, $length) {
  fseek($handle, $box['start']);
  return (string) fread($handle, min($length, $box['end'] - $box['start']));
}
