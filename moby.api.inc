<?php
/**
 * @file
 * REST API endpoint callbacks for Moby.
 *
 * The endpoints are described in moby_docs/API.md.
 */

/**
 * Returns the HTTP method of the current request.
 */
function moby_request_method() {
  return strtoupper($_SERVER['REQUEST_METHOD']);
}

/**
 * Decodes the JSON body of the current request.
 *
 * @return array|false
 *   The decoded request body, or FALSE if it is not a valid JSON object.
 */
function moby_request_json() {
  $data = json_decode(file_get_contents('php://input'), TRUE);
  return is_array($data) ? $data : FALSE;
}

/**
 * Returns a "404 Not Found" response.
 */
function moby_not_found() {
  return moby_error(404, 'not_found', t('The requested resource could not be found.'));
}

/**
 * Returns a "405 Method Not Allowed" response.
 *
 * @param array $allowed
 *   The HTTP methods supported by the endpoint.
 */
function moby_method_not_allowed(array $allowed) {
  return moby_error(405, 'method_not_allowed', t('The @method method is not supported by this endpoint.', array('@method' => moby_request_method())), NULL, array('Allow' => implode(', ', $allowed)));
}

/**
 * Returns the absolute URL of the site's favicon, as its pages link to it.
 */
function moby_favicon_url() {
  // The favicon can come from the theme, which API requests don't otherwise
  // need, so it may not be set up yet.
  if (empty($GLOBALS['theme'])) {
    backdrop_theme_initialize();
  }
  // The path is already an absolute URL, from file_create_url().
  $url = backdrop_get_favicon()['path'];

  // Favicons are usually cached for a long time, so add the file's
  // modification time. A changed favicon then has a new URL, which the app
  // loads instead of a copy it cached before.
  $base = $GLOBALS['base_url'] . '/';
  if (strpos($url, $base) === 0) {
    $path = BACKDROP_ROOT . '/' . rawurldecode(strtok(substr($url, strlen($base)), '?'));
    if (is_file($path)) {
      $url .= (strpos($url, '?') === FALSE ? '?' : '&') . 'v=' . filemtime($path);
    }
  }
  return $url;
}

/**
 * Page callback: GET /api/moby
 *
 * Lists the API's URLs, like the root of GitHub's API. The URLs are RFC 6570
 * URI templates, and clients add the "api" query parameter themselves.
 */
function moby_api_index() {
  // Backdrop passes any additional path segments as arguments.
  if (func_num_args()) {
    return moby_not_found();
  }
  if (moby_request_method() !== 'GET') {
    return moby_method_not_allowed(array('GET'));
  }
  // The template expressions are added after url(), which would encode them.
  $templates = array(
    'meta_url' => array('meta', ''),
    'auth_token_url' => array('auth/token', ''),
    'current_user_url' => array('me', ''),
    'media_url' => array('media', '{/id}'),
    'posts_url' => array('posts', '{/id}{?page,limit}'),
    'tags_url' => array('tags', '{?search,limit}'),
  );
  $urls = array();
  foreach ($templates as $key => $template) {
    $urls[$key] = url(MOBY_API_PATH . '/' . $template[0], array('absolute' => TRUE)) . $template[1];
  }
  return moby_response($urls);
}

/**
 * Page callback: GET /api/moby/meta
 *
 * Describes the site and its capabilities so the app can adapt its UI.
 */
function moby_api_meta() {
  // Backdrop passes any additional path segments as arguments.
  if (func_num_args()) {
    return moby_not_found();
  }
  if (moby_request_method() !== 'GET') {
    return moby_method_not_allowed(array('GET'));
  }

  $config = config('moby.settings');
  $mechanism = moby_get_auth_mechanism();
  $extensions = moby_media_extensions();
  $thumbnail_field = moby_video_thumbnail_field();
  $setup_issues = moby_setup_issues();
  $location_name = (bool) _moby_mapped_field($config->get('mapping.location.name_field'));
  $location_coordinates = (bool) moby_coordinates_field();
  $tags = moby_tags_vocabulary() && moby_tags_field();
  return moby_response(array(
    // The newest and oldest API versions this site serves. See "Versioning"
    // in API.md.
    'api_version' => MOBY_API_VERSION,
    'api_min_version' => MOBY_API_MIN_VERSION,
    'module' => moby_module_info(),
    'site' => array(
      'name' => config_get('system.core', 'site_name'),
      'url' => url('', array('absolute' => TRUE)),
      'favicon' => moby_favicon_url(),
    ),
    'setup_issues' => $setup_issues,
    'auth' => $mechanism ? $mechanism->getDiscoveryInfo() : NULL,
    'limits' => array(
      'max_upload_size' => moby_max_upload_size(),
      'max_media_per_post' => moby_max_media_per_post(),
      'max_description_length' => NULL,
      'max_alt_length' => MOBY_MAX_ALT_LENGTH,
      'max_tags_per_post' => MOBY_MAX_TAGS_PER_POST,
      'max_tag_length' => MOBY_MAX_TAG_LENGTH,
    ),
    'media' => array(
      'image' => array(
        'extensions' => $extensions['image'],
        'max_size' => moby_max_image_size(),
      ),
      'video' => array(
        'extensions' => $extensions['video'],
        'max_size' => moby_max_video_size(),
        // NULL if the site doesn't store video thumbnails.
        'thumbnail' => $thumbnail_field ? array(
          'extensions' => moby_video_thumbnail_extensions($thumbnail_field),
        ) : NULL,
      ),
    ),
    'description' => moby_description_format(),
    // The order posts show their parts in on the site, for apps to match.
    'display' => array(
      'order' => moby_post_order('default'),
    ),
    'features' => array(
      'location' => $location_name || $location_coordinates,
      'location_name' => $location_name,
      'location_coordinates' => $location_coordinates,
      'drafts' => TRUE,
      'tags' => $tags,
    ),
  ));
}

/**
 * Describes the module, for apps to show and for support. Apps don't compare
 * its version; they use the API version.
 */
function moby_module_info() {
  $info = system_get_info('module', 'moby');
  return array(
    'name' => 'moby',
    'platform' => 'backdrop',
    'version' => !empty($info['version']) ? $info['version'] : NULL,
  );
}

/**
 * Finds problems with the site's setup that stop posting from the app.
 *
 * Apps show these when the user adds a site, so they can tell an
 * administrator what to fix. Field and permission names are not included,
 * as discovery is public.
 *
 * @return array
 *   A list of issues, each an array with:
 *   - code: A machine-readable code.
 *   - severity: "error" if posting is not possible, or "warning" if posting
 *     works but some of what the app sends is not saved.
 *   - message: A description for people, which may be translated.
 */
function moby_setup_issues() {
  $issues = array();
  $add = function ($code, $severity, $message) use (&$issues) {
    $issues[] = array('code' => $code, 'severity' => $severity, 'message' => $message);
  };
  $settings_page = t('Configuration > Web services > Moby');
  $config = config('moby.settings');

  if (!moby_get_auth_mechanism()) {
    $add('auth_unavailable', 'error', t('Logging in from the app is not available, because the site\'s authentication mechanism is missing.'));
  }

  $node_type = $config->get('node_type');
  if (!$node_type || !node_type_get_type($node_type)) {
    $add('not_configured', 'error', t('Posting from the app has not been set up. A site administrator needs to select a content type at !page.', array('!page' => $settings_page)));
    // The mapping depends on the content type.
    return $issues;
  }

  $mapping = $config->get('mapping');
  if (!$mapping['description']['field']) {
    $add('description_not_mapped', 'warning', t('Descriptions will not be saved, because no description field is selected at !page.', array('!page' => $settings_page)));
  }

  // Mapped fields that have since been deleted from the content type.
  $mapped = array(
    'description' => array($mapping['description']['field'], t('description')),
    'date' => array($mapping['date']['field'] === 'created' ? '' : $mapping['date']['field'], t('date')),
    'location.name' => array($mapping['location']['name_field'], t('location name')),
  );
  foreach ($mapped as $property => list($field_name, $label)) {
    if ($field_name && !_moby_mapped_field($field_name)) {
      $add('field_missing', 'warning', t('The @label will not be saved, because its field no longer exists. A site administrator needs to choose another at !page.', array('@label' => $label, '!page' => $settings_page)));
    }
  }

  // Coordinates need the optional Geofield module. Without it, sites simply
  // don't support them (see features.location_coordinates in discovery), so
  // this is only a problem if a coordinates field was selected.
  if (!module_exists('geofield')) {
    if ($mapping['location']['coordinates_field']) {
      $add('geofield_unavailable', 'warning', t('Location coordinates will not be saved, because a coordinates field is selected but the Geofield module is not enabled.'));
    }
  }
  elseif ($mapping['location']['coordinates_field'] && !moby_coordinates_field()) {
    $add('field_missing', 'warning', t('The @label will not be saved, because its field no longer exists or is not a Geofield. A site administrator needs to choose another at !page.', array('@label' => t('location coordinates'), '!page' => $settings_page)));
  }

  if (!moby_media_field()) {
    $message = $mapping['media']['field']
      ? t('Photos and videos cannot be posted, because the media field no longer exists or is not a file field. A site administrator needs to choose another at !page.', array('!page' => $settings_page))
      : t('Photos and videos cannot be posted, because no media field is selected at !page.', array('!page' => $settings_page));
    $add('media_not_mapped', 'warning', $message);
  }
  else {
    $extensions = moby_media_extensions();
    if (!$extensions['image'] && !$extensions['video']) {
      $add('media_extensions', 'warning', t('Photos and videos cannot be posted, because the media field does not allow any photo or video file extensions.'));
    }
    elseif (!$extensions['video']) {
      $add('media_extensions', 'warning', t('Videos cannot be posted, because the media field does not allow any video file extensions.'));
    }
    elseif (!$extensions['image']) {
      $add('media_extensions', 'warning', t('Photos cannot be posted, because the media field does not allow any photo file extensions.'));
    }
  }

  if (!empty($mapping['tags']['vocabulary'])) {
    if (!moby_tags_vocabulary()) {
      $add('tags_unavailable', 'warning', t('Tags will not be saved, because the tag vocabulary or the Taxonomy module is missing. A site administrator needs to choose another vocabulary at !page.', array('!page' => $settings_page)));
    }
    elseif (!moby_tags_field()) {
      $add('field_missing', 'warning', t('Tags will not be saved, because the content type has no field for them. A site administrator needs to save the settings at !page, which adds one.', array('!page' => $settings_page)));
    }
  }

  if ($mapping['media']['thumbnail_field'] && !moby_video_thumbnail_field()) {
    $add('thumbnail_field_missing', 'warning', t('Video thumbnails will not be saved, because the thumbnail field is not an image field on the video file type. A site administrator needs to choose another at !page.', array('!page' => $settings_page)));
  }

  if (!image_style_load((string) $config->get('thumbnail_style'))) {
    $add('thumbnail_style_missing', 'warning', t('Thumbnails will be full size and slow to load, because the thumbnail image style no longer exists. A site administrator needs to choose another at !page.', array('!page' => $settings_page)));
  }

  return $issues;
}

/**
 * Page callback: POST|DELETE /api/moby/auth/token
 *
 * POST exchanges a username and password for an API token. DELETE revokes the
 * token used to authenticate the request.
 */
function moby_api_auth_token() {
  if (func_num_args()) {
    return moby_not_found();
  }
  switch (moby_request_method()) {
    case 'POST':
      return moby_api_auth_token_create();

    case 'DELETE':
      $record = moby_current_token();
      if (!$record) {
        return moby_error(401, 'unauthorized', t('Authentication is required.'), NULL, array('WWW-Authenticate' => 'Bearer'));
      }
      moby_token_delete($record->tid);
      return moby_response(NULL, 204);
  }
  return moby_method_not_allowed(array('POST', 'DELETE'));
}

/**
 * Handles POST /api/moby/auth/token.
 */
function moby_api_auth_token_create() {
  $data = moby_request_json();
  if ($data === FALSE) {
    return moby_error(400, 'invalid_json', t('The request body must be a JSON object.'));
  }
  $mechanism = moby_get_auth_mechanism();
  if (!$mechanism) {
    watchdog('moby', 'The configured authentication mechanism %name is not available.', array('%name' => config_get('moby.settings', 'auth_mechanism')), WATCHDOG_ERROR);
    return moby_error(503, 'auth_unavailable', t('Logging in is not currently available.'));
  }

  try {
    $account = $mechanism->authenticate($data);
  }
  catch (MobyAuthException $e) {
    return moby_error($e->getStatus(), $e->getErrorCode(), $e->getMessage(), $e->getField());
  }

  // Check the account can post before issuing a token it could not use. The
  // messages name the permission, as only the user who logged in sees them.
  if (!user_access('post via moby', $account)) {
    return moby_error(403, 'forbidden', t('This account is not allowed to post using the app. A site administrator needs to grant it the "@permission" permission.', array('@permission' => t('Post via Moby'))));
  }
  $node_type = node_type_get_type(config_get('moby.settings', 'node_type'));
  if ($node_type && !node_access('create', $node_type->type, $account)) {
    return moby_error(403, 'forbidden', t('This account is not allowed to create posts. A site administrator needs to grant it the "@permission" permission.', array('@permission' => t('@type: Create new content', array('@type' => $node_type->name)))));
  }

  $device_name = (isset($data['device_name']) && is_string($data['device_name'])) ? trim($data['device_name']) : '';
  if ($device_name === '') {
    $device_name = t('Mobile device');
  }
  $token = moby_token_create($account, $device_name, $mechanism->getName());

  // Record the login as the login form does. This also invalidates any
  // outstanding one-time login links.
  db_update('users')
    ->fields(array('login' => REQUEST_TIME))
    ->condition('uid', $account->uid)
    ->execute();
  watchdog('moby', 'API token issued to %name for %device.', array('%name' => $account->name, '%device' => $device_name));

  return moby_response(array(
    'token' => $token,
    'token_type' => 'Bearer',
    'user' => moby_user_data($account),
  ), 201);
}

/**
 * Page callback: GET /api/moby/me
 */
function moby_api_me() {
  if (func_num_args()) {
    return moby_not_found();
  }
  if (moby_request_method() !== 'GET') {
    return moby_method_not_allowed(array('GET'));
  }

  global $user;
  return moby_response(moby_user_data($user));
}

/**
 * Returns the API representation of a user account.
 *
 * @param User $account
 *   The user account.
 */
function moby_user_data($account) {
  return array(
    'id' => (int) $account->uid,
    'name' => $account->name,
    'url' => url('user/' . $account->uid, array('absolute' => TRUE)),
    'permissions' => array(
      'create' => moby_user_can_create($account),
      'publish' => moby_user_can_publish($account),
    ),
  );
}

/**
 * Page callback: /api/moby/media[/{id}]
 *
 * - POST /media uploads a single photo or video as a pending media item.
 * - GET /media/{id} returns a media item.
 * - DELETE /media/{id} deletes a pending media item.
 *
 * @param string|null $media_id
 *   The media (file) ID, if provided in the path.
 */
function moby_api_media($media_id = NULL) {
  if (func_num_args() > 1) {
    return moby_not_found();
  }
  $method = moby_request_method();

  if (!isset($media_id)) {
    if ($method !== 'POST') {
      return moby_method_not_allowed(array('POST'));
    }
    return moby_api_media_upload();
  }

  $media = ctype_digit((string) $media_id) ? moby_media_load($media_id) : FALSE;
  if (!$media) {
    return moby_not_found();
  }

  switch ($method) {
    case 'GET':
      return moby_response(moby_media_data($media['file'], $media['alt']));

    case 'DELETE':
      global $user;
      if ($media['file']->uid != $user->uid) {
        return moby_error(403, 'forbidden', t('Only the uploader may delete this media item.'));
      }
      if ($media['file']->status == FILE_STATUS_PERMANENT) {
        return moby_error(409, 'media_attached', t('This media item is attached to a post. Remove it by updating the post instead.'));
      }
      file_delete($media['file']->fid);
      return moby_response(NULL, 204);
  }
  return moby_method_not_allowed(array('GET', 'DELETE'));
}

/**
 * Handles POST /api/moby/media.
 *
 * The upload is checked here first so that specific API errors can be
 * returned, then saved by file_save_upload(), which applies the same
 * validation and filename security measures as the media field's widget.
 */
function moby_api_media_upload() {
  $media_field = moby_media_field();
  if (!$media_field) {
    return moby_error(503, 'not_configured', t('Uploading photos and videos has not been configured on this site.'));
  }

  // When a request exceeds post_max_size, PHP discards the whole body.
  if (empty($_FILES) && isset($_SERVER['CONTENT_LENGTH']) && $_SERVER['CONTENT_LENGTH'] > parse_size(ini_get('post_max_size'))) {
    return moby_error(413, 'payload_too_large', t('The upload exceeds the maximum size of @size.', array('@size' => format_size(moby_max_upload_size()))), 'file');
  }
  if (!isset($_FILES['file']['name']) || !is_string($_FILES['file']['name'])) {
    return moby_error(422, 'validation_failed', t('A single file must be uploaded in the "file" field.'), 'file');
  }
  $upload = $_FILES['file'];
  switch ($upload['error']) {
    case UPLOAD_ERR_OK:
      break;
    case UPLOAD_ERR_INI_SIZE:
    case UPLOAD_ERR_FORM_SIZE:
      return moby_error(413, 'payload_too_large', t('The upload exceeds the maximum size of @size.', array('@size' => format_size(moby_max_upload_size()))), 'file');
    case UPLOAD_ERR_PARTIAL:
    case UPLOAD_ERR_NO_FILE:
      return moby_error(422, 'validation_failed', t('The upload did not complete.'), 'file');
    default:
      watchdog('moby', 'Upload of %file failed with PHP error code @code.', array('%file' => $upload['name'], '@code' => $upload['error']), WATCHDOG_ERROR);
      return moby_error(500, 'server_error', t('The upload could not be saved.'), 'file');
  }

  $alt = isset($_POST['alt']) && is_string($_POST['alt']) ? trim($_POST['alt']) : '';
  if (backdrop_strlen($alt) > MOBY_MAX_ALT_LENGTH) {
    return moby_error(422, 'validation_failed', t('Alt text may not be longer than @max characters.', array('@max' => MOBY_MAX_ALT_LENGTH)), 'alt');
  }

  // The type is determined from the extension, the same way Backdrop sets the
  // MIME type of saved files.
  $media_type = moby_media_type(file_get_mimetype($upload['name']));

  // A video may include a thumbnail image, such as a frame chosen in the app.
  // It is ignored if the site doesn't store thumbnails.
  $thumbnail_upload = NULL;
  if (isset($_FILES['thumbnail'])) {
    if (!is_string($_FILES['thumbnail']['name'])) {
      return moby_error(422, 'validation_failed', t('A single thumbnail must be uploaded in the "thumbnail" field.'), 'thumbnail');
    }
    if ($media_type !== 'video') {
      return moby_error(422, 'validation_failed', t('Only videos may have a thumbnail.'), 'thumbnail');
    }
    switch ($_FILES['thumbnail']['error']) {
      case UPLOAD_ERR_OK:
        if (moby_video_thumbnail_field()) {
          $thumbnail_upload = $_FILES['thumbnail'];
          if ($thumbnail_upload['size'] > moby_max_image_size()) {
            return moby_error(413, 'payload_too_large', t('The thumbnail is @filesize, exceeding the maximum size of @maxsize.', array('@filesize' => format_size($thumbnail_upload['size']), '@maxsize' => format_size(moby_max_image_size()))), 'thumbnail');
          }
        }
        break;
      case UPLOAD_ERR_INI_SIZE:
      case UPLOAD_ERR_FORM_SIZE:
        return moby_error(413, 'payload_too_large', t('The thumbnail exceeds the maximum size of @size.', array('@size' => format_size(moby_max_image_size()))), 'thumbnail');
      default:
        return moby_error(422, 'validation_failed', t('The thumbnail upload did not complete.'), 'thumbnail');
    }
  }
  $extensions = moby_media_extensions();
  $extension = strtolower(pathinfo($upload['name'], PATHINFO_EXTENSION));
  if (!$media_type || !in_array($extension, $extensions[$media_type])) {
    $allowed = array_merge($extensions['image'], $extensions['video']);
    return moby_error(415, 'unsupported_media_type', t('Only photos and videos with the following extensions may be uploaded: @extensions.', array('@extensions' => implode(' ', $allowed))), 'file');
  }

  $max_size = ($media_type === 'video') ? moby_max_video_size() : moby_max_image_size();
  if ($upload['size'] > $max_size) {
    return moby_error(413, 'payload_too_large', t('The file is @filesize, exceeding the maximum size of @maxsize.', array('@filesize' => format_size($upload['size']), '@maxsize' => format_size($max_size))), 'file');
  }

  $destination = file_field_widget_uri($media_field['field'], $media_field['instance']);
  if (!file_prepare_directory($destination, FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
    watchdog('moby', 'The upload directory %directory could not be created or is not writable.', array('%directory' => $destination), WATCHDOG_ERROR);
    return moby_error(500, 'server_error', t('The upload could not be saved.'), 'file');
  }

  // file_save_upload() reads uploads from the structure used by forms. The
  // "moby_media" source is used by moby_file_presave() to set the file type.
  foreach (array('name', 'type', 'tmp_name', 'error', 'size') as $key) {
    $_FILES['files'][$key]['moby_media'] = $upload[$key];
  }
  $validators = file_field_widget_upload_validators($media_field['field'], $media_field['instance']);
  $validators['file_validate_size'] = array($max_size);
  $file = file_save_upload('moby_media', $validators, $destination);

  // Errors are reported as messages, which would otherwise be shown on a
  // later page.
  $messages = backdrop_get_messages();
  if (!$file) {
    $message = !empty($messages['error']) ? trim(decode_entities(strip_tags(reset($messages['error'])))) : t('The upload could not be saved.');
    return moby_error(422, 'validation_failed', $message, 'file');
  }

  if ($media_type === 'video') {
    $changed = FALSE;

    // Core stores the dimensions of images in the file metadata, but not of
    // videos. Store them for videos uploaded through the API, along with
    // their duration, if they can be read from the MP4 or QuickTime header.
    module_load_include('inc', 'moby', 'includes/moby.video');
    if ($video_info = moby_video_get_info($file->uri)) {
      $file->metadata = $video_info + (isset($file->metadata) ? $file->metadata : array());
      $changed = TRUE;
    }

    // Save the thumbnail into the image field on the video's file entity.
    // Saving the video records the thumbnail's usage, which makes it
    // permanent. When the video is deleted, including when an unused upload
    // expires, that usage is removed and cron deletes the thumbnail.
    if ($thumbnail_upload && ($thumbnail_field = moby_video_thumbnail_field($file->type))) {
      $thumbnail = moby_video_thumbnail_save($thumbnail_upload, $thumbnail_field);
      if (!$thumbnail instanceof File) {
        // Don't leave behind a video the app will upload again.
        file_delete($file->fid);
        return moby_error(422, 'validation_failed', $thumbnail, 'thumbnail');
      }
      $field_name = $thumbnail_field['field']['field_name'];
      $file->{$field_name}[LANGUAGE_NONE] = array(array(
        'fid' => $thumbnail->fid,
        'alt' => backdrop_substr($alt, 0, 512),
        'width' => isset($thumbnail->metadata['width']) ? $thumbnail->metadata['width'] : NULL,
        'height' => isset($thumbnail->metadata['height']) ? $thumbnail->metadata['height'] : NULL,
      ));
      $changed = TRUE;
    }

    if ($changed) {
      $file->save();
    }
  }

  db_insert('moby_media')
    ->fields(array(
      'fid' => $file->fid,
      'alt' => $alt,
    ))
    ->execute();

  return moby_response(moby_media_data($file, $alt), 201, array('Location' => url(MOBY_API_PATH . '/media/' . $file->fid, array('absolute' => TRUE))));
}

/**
 * Saves an uploaded video thumbnail.
 *
 * The thumbnail is validated with the image field's settings, as the field's
 * widget would: allowed extensions, maximum file size, and image dimensions
 * (larger images are scaled down if the field has maximum dimensions).
 *
 * @param array $upload
 *   The thumbnail's entry in $_FILES.
 * @param array $thumbnail_field
 *   The image field and instance, from moby_video_thumbnail_field().
 *
 * @return File|string
 *   The saved, temporary file, or an error message.
 */
function moby_video_thumbnail_save(array $upload, array $thumbnail_field) {
  $destination = file_field_widget_uri($thumbnail_field['field'], $thumbnail_field['instance']);
  if (!file_prepare_directory($destination, FILE_CREATE_DIRECTORY | FILE_MODIFY_PERMISSIONS)) {
    watchdog('moby', 'The upload directory %directory could not be created or is not writable.', array('%directory' => $destination), WATCHDOG_ERROR);
    return t('The thumbnail could not be saved.');
  }
  foreach (array('name', 'type', 'tmp_name', 'error', 'size') as $key) {
    $_FILES['files'][$key]['moby_thumbnail'] = $upload[$key];
  }
  $file = file_save_upload('moby_thumbnail', moby_video_thumbnail_validators($thumbnail_field), $destination);
  $messages = backdrop_get_messages();
  if (!$file) {
    return !empty($messages['error']) ? trim(decode_entities(strip_tags(reset($messages['error'])))) : t('The thumbnail could not be saved.');
  }
  return $file;
}

/**
 * Returns the upload validators for video thumbnails.
 *
 * These match the validators used by the image field's widget.
 *
 * @param array $thumbnail_field
 *   The image field and instance, from moby_video_thumbnail_field().
 *
 * @return array
 *   Validators for file_save_upload().
 */
function moby_video_thumbnail_validators(array $thumbnail_field) {
  $settings = $thumbnail_field['instance']['settings'];
  $validators = file_field_widget_upload_validators($thumbnail_field['field'], $thumbnail_field['instance']);
  $validators['file_validate_is_image'] = array();
  if (!empty($settings['max_dimensions']) || !empty($settings['min_dimensions'])) {
    $validators['file_validate_image_resolution'] = array($settings['max_dimensions'], $settings['min_dimensions']);
  }
  $validators['file_validate_extensions'] = array(implode(' ', moby_video_thumbnail_extensions($thumbnail_field)));
  $validators['file_validate_size'] = array(moby_max_image_size());
  return $validators;
}

/**
 * Returns the extensions allowed for video thumbnails.
 *
 * @param array $thumbnail_field
 *   The image field and instance, from moby_video_thumbnail_field().
 *
 * @return array
 *   The extensions allowed by the image field that the server can process.
 */
function moby_video_thumbnail_extensions(array $thumbnail_field) {
  $supported = moby_image_supported_extensions();
  $allowed = preg_split('/\s+/', trim($thumbnail_field['instance']['settings']['file_extensions']), -1, PREG_SPLIT_NO_EMPTY);
  return array_values($allowed ? array_intersect($allowed, $supported) : $supported);
}

/**
 * Returns the image extensions the server can process.
 *
 * @return array
 *   Lowercase file extensions.
 */
function moby_image_supported_extensions() {
  $supported = image_get_supported_extensions();
  backdrop_alter('image_supported_extensions', $supported);
  return $supported;
}

/**
 * Loads a media item uploaded through the API.
 *
 * Pending media is only available to its uploader. Attached media is also
 * available to anyone who may view the file.
 *
 * @param int $fid
 *   The file ID.
 *
 * @return array|false
 *   An array containing the "file" entity and its "alt" text, or FALSE if the
 *   file was not uploaded through the API or the user may not view it.
 */
function moby_media_load($fid) {
  global $user;
  $alt = db_query('SELECT alt FROM {moby_media} WHERE fid = :fid', array(':fid' => $fid))->fetchField();
  if ($alt === FALSE || !($file = file_load($fid))) {
    return FALSE;
  }
  $is_owner = ($file->uid == $user->uid);
  if (!$is_owner && ($file->status != FILE_STATUS_PERMANENT || !file_access('view', $file))) {
    return FALSE;
  }
  return array('file' => $file, 'alt' => (string) $alt);
}

/**
 * Returns the API representation of a media item.
 *
 * @param File $file
 *   The file entity.
 * @param string $alt
 *   The media item's alt text.
 */
function moby_media_data($file, $alt) {
  $media_type = moby_media_type($file->filemime);
  // Dimensions are stored by core for images, and by moby_api_media_upload()
  // for videos, along with their duration.
  $metadata = (isset($file->metadata) ? $file->metadata : array()) + array(
    'width' => NULL,
    'height' => NULL,
    'duration' => NULL,
  );
  return array(
    'id' => (int) $file->fid,
    'type' => $media_type,
    'status' => ($file->status == FILE_STATUS_PERMANENT) ? 'attached' : 'pending',
    'filename' => $file->filename,
    'mime_type' => $file->filemime,
    'size' => (int) $file->filesize,
    'width' => isset($metadata['width']) ? (int) $metadata['width'] : NULL,
    'height' => isset($metadata['height']) ? (int) $metadata['height'] : NULL,
    'duration' => isset($metadata['duration']) ? (float) $metadata['duration'] : NULL,
    'url' => file_create_url($file->uri),
    'thumbnail' => ($media_type === 'video') ? moby_video_thumbnail_data($file) : moby_thumbnail_data($file->uri, $metadata['width'], $metadata['height']),
    'alt' => $alt,
  );
}

/**
 * Returns the API representation of a video's thumbnail.
 *
 * @param File $file
 *   The video's file entity.
 *
 * @return array|null
 *   An array with the thumbnail's "url", "width", and "height", or NULL if the
 *   video has no thumbnail.
 */
function moby_video_thumbnail_data($file) {
  $thumbnail_field = moby_video_thumbnail_field($file->type);
  if (!$thumbnail_field) {
    return NULL;
  }
  $items = field_get_items('file', $file, $thumbnail_field['field']['field_name']);
  if (empty($items[0]['fid']) || !($thumbnail = file_load($items[0]['fid']))) {
    return NULL;
  }
  return moby_thumbnail_data($thumbnail->uri, isset($items[0]['width']) ? $items[0]['width'] : NULL, isset($items[0]['height']) ? $items[0]['height'] : NULL);
}

/**
 * Returns the API representation of an image at the thumbnail image style.
 *
 * If the configured image style no longer exists, the image is returned at its
 * original size, which is reported as a setup issue.
 *
 * @param string $uri
 *   The URI of the original image.
 * @param int|null $width
 *   The width of the original image, if known.
 * @param int|null $height
 *   The height of the original image, if known.
 *
 * @return array|null
 *   An array with the thumbnail's "url", "width", and "height", or NULL if the
 *   server can't process this type of image, such as HEIC.
 */
function moby_thumbnail_data($uri, $width, $height) {
  // SVGs aren't processed. Core's image_style_url() returns their original URL.
  $extension = strtolower(pathinfo($uri, PATHINFO_EXTENSION));
  if (!in_array($extension, moby_image_supported_extensions()) && !image_is_svg($uri)) {
    return NULL;
  }

  $dimensions = array('width' => $width, 'height' => $height);
  $style_name = (string) config_get('moby.settings', 'thumbnail_style');
  if (!image_style_load($style_name)) {
    $url = file_create_url($uri);
  }
  else {
    $url = image_style_url($style_name, $uri);
    // Effects can't calculate dimensions from unknown ones or an SVG's.
    if (isset($width, $height) && !image_is_svg($uri)) {
      image_style_transform_dimensions($style_name, $dimensions);
    }
  }
  return array(
    'url' => $url,
    'width' => isset($dimensions['width']) ? (int) $dimensions['width'] : NULL,
    'height' => isset($dimensions['height']) ? (int) $dimensions['height'] : NULL,
  );
}

/**
 * Page callback: /api/moby/posts[/{id}]
 *
 * - GET /posts lists the user's posts, newest first.
 * - POST /posts creates a post.
 * - GET /posts/{id} returns a post.
 * - PATCH /posts/{id} updates a post.
 * - DELETE /posts/{id} deletes a post.
 *
 * @param string|null $post_id
 *   The post (node) ID, if provided in the path.
 */
function moby_api_posts($post_id = NULL) {
  if (func_num_args() > 1) {
    return moby_not_found();
  }
  $method = moby_request_method();

  if (!isset($post_id)) {
    switch ($method) {
      case 'GET':
        return moby_api_posts_list();
      case 'POST':
        return moby_api_posts_create();
    }
    return moby_method_not_allowed(array('GET', 'POST'));
  }

  if (!ctype_digit((string) $post_id)) {
    return moby_not_found();
  }

  switch ($method) {
    case 'GET':
      $node = moby_post_load($post_id);
      if (!$node) {
        return moby_not_found();
      }
      return moby_response(moby_post_data($node));

    // POST updates too, for clients that can't send PATCH, such as Android's
    // HttpURLConnection.
    case 'PATCH':
    case 'POST':
      return moby_api_posts_update($post_id);

    case 'DELETE':
      return moby_api_posts_delete($post_id);
  }
  return moby_method_not_allowed(array('GET', 'PATCH', 'POST', 'DELETE'));
}

/**
 * Handles GET /api/moby/posts.
 */
function moby_api_posts_list() {
  global $user;

  $node_type = config_get('moby.settings', 'node_type');
  if (!$node_type || !node_type_get_type($node_type)) {
    return moby_error(503, 'not_configured', t('Posting has not been configured on this site.'));
  }

  $limit = moby_query_int('limit', 20, 1, MOBY_MAX_POSTS_PER_PAGE);
  if (is_array($limit)) {
    return $limit;
  }
  $page = moby_query_int('page', 0, 0, MOBY_MAX_POSTS_PAGE);
  if (is_array($page)) {
    return $page;
  }

  $query = moby_posts_query($node_type, $user);
  $total = (int) $query->countQuery()->execute()->fetchField();

  $posts = array();
  $nids = $query->range($page * $limit, $limit)->execute()->fetchCol();
  if ($nids) {
    // node_load_multiple() keys nodes by nid, so keep the query's order.
    $nodes = node_load_multiple($nids);
    foreach ($nids as $nid) {
      if (isset($nodes[$nid])) {
        $posts[] = moby_post_data($nodes[$nid]);
      }
    }
  }

  return moby_response(array(
    'posts' => $posts,
    'page' => $page,
    'limit' => $limit,
    'total' => $total,
  ));
}

/**
 * Page callback: GET /api/moby/tags
 *
 * Suggests tags for a post as the user types. Tags starting with the
 * "search" parameter come first, then tags containing it, each with the most
 * used first. With no "search", the most used tags are returned. ("q" can't
 * be used, as Backdrop keeps the page's path in it.)
 */
function moby_api_tags() {
  if (func_num_args()) {
    return moby_not_found();
  }
  if (moby_request_method() !== 'GET') {
    return moby_method_not_allowed(array('GET'));
  }
  $limit = moby_query_int('limit', 10, 1, MOBY_MAX_TAG_SUGGESTIONS);
  if (is_array($limit)) {
    return $limit;
  }
  $search = (isset($_GET['search']) && is_string($_GET['search'])) ? trim(ltrim(trim($_GET['search']), '#')) : '';
  $vocabulary = moby_tags_vocabulary();
  if (!$vocabulary || !moby_tags_field()) {
    return moby_response(array('tags' => array()));
  }

  $names = array();
  $patterns = ($search === '') ? array('%') : array(db_like($search) . '%', '%' . db_like($search) . '%');
  foreach ($patterns as $pattern) {
    if (count($names) >= $limit) {
      break;
    }
    $query = db_select('taxonomy_term_data', 't');
    $query->leftJoin('taxonomy_index', 'i', 'i.tid = t.tid');
    $query->fields('t', array('name'))
      ->condition('t.vocabulary', $vocabulary)
      ->condition('t.name', $pattern, 'LIKE')
      ->groupBy('t.tid')
      ->groupBy('t.name')
      ->orderBy('uses', 'DESC')
      ->orderBy('t.name')
      ->range(0, $limit + count($names))
      ->addTag('term_access');
    $query->addExpression('COUNT(i.nid)', 'uses');
    foreach ($query->execute()->fetchCol() as $name) {
      $names[backdrop_strtolower($name)] = $name;
    }
  }
  return moby_response(array('tags' => array_slice(array_values($names), 0, $limit)));
}

/**
 * Reads a whole-number query string parameter.
 *
 * Out-of-range values are rejected rather than adjusted, so that a bug in an
 * app doesn't silently skip posts.
 *
 * @param string $name
 *   The parameter name.
 * @param int $default
 *   The value to use if the parameter is not provided.
 * @param int $min
 *   The minimum value.
 * @param int $max
 *   The maximum value.
 *
 * @return int|array
 *   The value, or an error response.
 */
function moby_query_int($name, $default, $min, $max) {
  if (!isset($_GET[$name])) {
    return $default;
  }
  $value = $_GET[$name];
  // The length check keeps huge numbers from overflowing before comparison.
  if (is_string($value) && ctype_digit($value) && strlen($value) <= strlen((string) $max) && (int) $value >= $min && (int) $value <= $max) {
    return (int) $value;
  }
  return moby_error(422, 'validation_failed', t('The @name must be a whole number from @min to @max.', array('@name' => $name, '@min' => $min, '@max' => $max)), $name);
}

/**
 * Builds the query for a user's posts, newest date first.
 *
 * Only posts the user may view are included. Unpublished posts follow the
 * same rules as node_access(): they need "View own unpublished content" or
 * "Bypass content access control". Grants from node access modules are
 * applied through the "node_access" query tag.
 *
 * @param string $node_type
 *   The configured content type.
 * @param User $account
 *   The user whose posts to list.
 *
 * @return SelectQuery
 *   A query selecting node IDs, in display order.
 */
function moby_posts_query($node_type, $account) {
  $query = db_select('node', 'n')
    ->fields('n', array('nid'))
    ->condition('n.type', $node_type)
    ->condition('n.uid', $account->uid)
    ->addTag('node_access')
    ->addMetaData('account', $account);

  if (!user_access('bypass node access', $account) && !user_access('view own unpublished content', $account)) {
    $query->condition('n.status', NODE_PUBLISHED);
  }

  // Sort by the post date, which is "created" unless it is mapped to a date
  // field. Date fields store UTC values whose sort order is chronological
  // for each storage type (ISO strings, DATETIME, or Unix timestamps). Posts
  // with an empty date field go last, as they have no date to sort by.
  $date_field = config_get('moby.settings', 'mapping.date.field');
  if ($date_field !== 'created' && ($field_name = _moby_mapped_field($date_field))) {
    $field = field_info_field($field_name);
    $table = _field_sql_storage_tablename($field);
    $column = _field_sql_storage_columnname($field_name, 'value');
    // Posts are saved with LANGUAGE_NONE values. Matching one language and
    // delta keeps it to one row per node.
    $alias = $query->leftJoin($table, 'd', "d.entity_type = 'node' AND d.entity_id = n.nid AND d.deleted = 0 AND d.delta = 0 AND d.language = :moby_language", array(':moby_language' => LANGUAGE_NONE));
    $query->addExpression("CASE WHEN $alias.$column IS NULL THEN 1 ELSE 0 END", 'moby_no_date');
    $query->orderBy('moby_no_date');
    $query->orderBy("$alias.$column", 'DESC');
  }
  $query->orderBy('n.created', 'DESC');
  // Posts with the same date are listed newest first, consistently between
  // pages.
  $query->orderBy('n.nid', 'DESC');

  return $query;
}

/**
 * Handles POST /api/moby/posts.
 */
function moby_api_posts_create() {
  global $user;

  $data = moby_request_json();
  if ($data === FALSE) {
    return moby_error(400, 'invalid_json', t('The request body must be a JSON object.'));
  }
  $node_type = config_get('moby.settings', 'node_type');
  if (!$node_type || !node_type_get_type($node_type)) {
    return moby_error(503, 'not_configured', t('Posting has not been configured on this site.'));
  }
  if (!node_access('create', $node_type, $user)) {
    return moby_error(403, 'forbidden', t('You do not have permission to create posts.'));
  }

  // A repeated request with the same client ID returns the existing post.
  if (isset($data['client_id'])) {
    if (!is_string($data['client_id']) || !preg_match('/^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i', $data['client_id'])) {
      return moby_error(422, 'validation_failed', t('The client_id must be a UUID.'), 'client_id');
    }
    $data['client_id'] = strtolower($data['client_id']);
    if ($existing = moby_post_load_by_client_id($user->uid, $data['client_id'])) {
      return moby_response(moby_post_data($existing));
    }
  }

  $values = moby_post_validate($data, $user);
  if (isset($values['#moby_response'])) {
    return $values;
  }

  $node = entity_create('node', array('type' => $node_type));
  node_object_prepare($node);
  $values['title'] = moby_post_generate_title($values['description'], $values['timestamp']);
  moby_post_apply($node, $values);

  try {
    field_attach_validate('node', $node);
  }
  catch (FieldValidationException $e) {
    return moby_post_field_error($e);
  }

  // Saving the node and recording its client ID happen together, so that a
  // concurrent request with the same client ID cannot create a duplicate.
  // Saving the node also marks its media permanent, as file_field_insert()
  // records the files' usage, so media stays pending if anything fails.
  $transaction = db_transaction();
  try {
    $node->save();
    if (isset($values['client_id'])) {
      db_insert('moby_post')
        ->fields(array(
          'nid' => $node->nid,
          'uid' => $user->uid,
          'client_id' => $values['client_id'],
          'created' => REQUEST_TIME,
        ))
        ->execute();
    }
  }
  catch (PDOException $e) {
    $transaction->rollback();
    if (isset($values['client_id']) && ($existing = moby_post_load_by_client_id($user->uid, $values['client_id']))) {
      return moby_response(moby_post_data($existing));
    }
    watchdog_exception('moby', $e);
    return moby_error(500, 'server_error', t('The post could not be saved.'));
  }
  unset($transaction);

  moby_post_sync_alt($values['media']);

  // Forms clear the page cache when submitted. The API has to do it itself.
  cache_clear_all();

  watchdog('moby', '@type: added %title.', array('@type' => $node->type, '%title' => $node->title), WATCHDOG_NOTICE, l(t('view'), 'node/' . $node->nid));

  return moby_response(moby_post_data(node_load($node->nid, NULL, TRUE)), 201, array('Location' => url(MOBY_API_PATH . '/posts/' . $node->nid, array('absolute' => TRUE))));
}

/**
 * Handles PATCH /api/moby/posts/{id}.
 *
 * @param string $post_id
 *   The post (node) ID.
 */
function moby_api_posts_update($post_id) {
  global $user;

  $node = moby_post_load($post_id);
  if (!$node) {
    return moby_not_found();
  }
  if (!node_access('update', $node, $user)) {
    return moby_error(403, 'forbidden', t('You do not have permission to edit this post.'));
  }
  $data = moby_request_json();
  if ($data === FALSE) {
    return moby_error(400, 'invalid_json', t('The request body must be a JSON object.'));
  }

  $values = moby_post_validate($data, $user, $node);
  if (isset($values['#moby_response'])) {
    return $values;
  }
  if (!$values) {
    return moby_response(moby_post_data($node));
  }

  // The title is generated from the description, or from the date if there is
  // no description. It is only regenerated when one of those changes, so a
  // title written on the site is kept when an app sends unchanged values.
  $old_description = moby_post_description($node);
  $old_timestamp = moby_post_timestamp($node);
  $description = isset($values['description']) ? $values['description'] : $old_description;
  $timestamp = isset($values['timestamp']) ? $values['timestamp'] : $old_timestamp;
  if ($description !== $old_description || ($description === '' && $timestamp !== $old_timestamp)) {
    $values['title'] = moby_post_generate_title($description, $timestamp);
  }

  // Follow the content type's revision setting, as the node form does.
  $node_type = node_type_get_type($node->type);
  $node->revision = $node_type->settings['revision_enabled'] && $node_type->settings['revision_default'];
  moby_post_apply($node, $values);

  try {
    field_attach_validate('node', $node);
  }
  catch (FieldValidationException $e) {
    return moby_post_field_error($e);
  }

  // Saving updates the media's file usage. New media becomes permanent. Media
  // removed from the post and not used elsewhere becomes temporary, and is
  // deleted by cron.
  $transaction = db_transaction();
  try {
    $node->save();
  }
  catch (Exception $e) {
    $transaction->rollback();
    watchdog_exception('moby', $e);
    return moby_error(500, 'server_error', t('The post could not be saved.'));
  }
  unset($transaction);

  if (isset($values['media'])) {
    moby_post_sync_alt($values['media']);
  }

  // Forms clear the page cache when submitted. The API has to do it itself.
  cache_clear_all();

  watchdog('moby', '@type: updated %title.', array('@type' => $node->type, '%title' => $node->title), WATCHDOG_NOTICE, l(t('view'), 'node/' . $node->nid));

  return moby_response(moby_post_data(node_load($node->nid, NULL, TRUE)));
}

/**
 * Handles DELETE /api/moby/posts/{id}.
 *
 * @param string $post_id
 *   The post (node) ID.
 */
function moby_api_posts_delete($post_id) {
  global $user;

  $node = moby_post_load($post_id);
  if (!$node) {
    return moby_not_found();
  }
  if (!node_access('delete', $node, $user)) {
    return moby_error(403, 'forbidden', t('You do not have permission to delete this post.'));
  }

  // Deleting the node removes its media's file usage. Media not used elsewhere
  // becomes temporary and is deleted by cron, as when deleting on the site.
  try {
    node_delete($node->nid);

    // Core only removes the usage of files on the node's current revision.
    // Files that only older revisions used would otherwise stay forever.
    $fids = db_query("SELECT fid FROM {file_usage} WHERE module = 'file' AND type = 'node' AND id = :nid", array(':nid' => $node->nid))->fetchCol();
    foreach (file_load_multiple($fids) as $file) {
      file_usage_delete($file, 'file', 'node', $node->nid, 0);
    }
  }
  catch (Exception $e) {
    watchdog_exception('moby', $e);
    return moby_error(500, 'server_error', t('The post could not be deleted.'));
  }

  // Forms clear the page cache when submitted. The API has to do it itself.
  cache_clear_all();

  watchdog('moby', '@type: deleted %title.', array('@type' => $node->type, '%title' => $node->title));

  return moby_response(NULL, 204);
}

/**
 * Keeps the stored alt text of media in sync with the text saved on a post.
 *
 * @param array $media
 *   Media values from moby_post_validate().
 */
function moby_post_sync_alt(array $media) {
  foreach ($media as $item) {
    db_update('moby_media')
      ->fields(array('alt' => $item['alt']))
      ->condition('fid', $item['fid'])
      ->execute();
  }
}

/**
 * Validates the properties of a request to create or update a post.
 *
 * @param array $data
 *   The decoded request body.
 * @param User $account
 *   The user making the request.
 * @param Node|null $node
 *   (optional) The post being updated. When updating, only the properties in
 *   the request are validated and returned, and properties whose fields are
 *   not mapped or that the user may not edit are left out.
 *
 * @return array
 *   Either an error response from moby_error(), or the validated values:
 *   - client_id: The client ID, if provided.
 *   - description: The description, or an empty string.
 *   - timestamp: The post date as a Unix timestamp.
 *   - location: An array with "name", "latitude", and "longitude" keys, each
 *     possibly NULL.
 *   - media: A list of arrays with "fid", "alt", and "display" keys, in order.
 *   - status: 1 for published or 0 for a draft.
 */
function moby_post_validate(array $data, $account, $node = NULL) {
  $is_update = isset($node);
  $values = array();
  if (!$is_update && isset($data['client_id'])) {
    $values['client_id'] = $data['client_id'];
  }

  // When updating, leaving a property out keeps its value, so null has no
  // meaning except for the location, where it clears the location.
  if ($is_update) {
    foreach (array('description', 'date', 'media', 'status', 'tags') as $property) {
      if (array_key_exists($property, $data) && !isset($data[$property])) {
        return moby_error(422, 'validation_failed', t('The @property may not be null. Leave it out to keep the current value.', array('@property' => $property)), $property);
      }
    }
  }

  // Description.
  if (!$is_update) {
    $values['description'] = '';
  }
  if (isset($data['description'])) {
    if (!is_string($data['description'])) {
      return moby_error(422, 'validation_failed', t('The description must be a string.'), 'description');
    }
    $values['description'] = trim($data['description']);
  }

  // Date.
  if (!$is_update) {
    $values['timestamp'] = REQUEST_TIME;
  }
  if (isset($data['date'])) {
    $timestamp = moby_parse_date($data['date']);
    if ($timestamp === FALSE) {
      return moby_error(422, 'validation_failed', t('The date must be an ISO 8601 date and time with a UTC offset, such as 2026-10-27T18:30:00-06:00.'), 'date');
    }
    $values['timestamp'] = $timestamp;
  }

  // Location. When updating, a location replaces the whole current location.
  if (!$is_update || array_key_exists('location', $data)) {
    $values['location'] = array('name' => NULL, 'latitude' => NULL, 'longitude' => NULL);
  }
  if (isset($data['location'])) {
    $location = $data['location'];
    if (!is_array($location)) {
      return moby_error(422, 'validation_failed', t('The location must be an object.'), 'location');
    }
    if (isset($location['name'])) {
      if (!is_string($location['name'])) {
        return moby_error(422, 'validation_failed', t('The location name must be a string.'), 'location.name');
      }
      $values['location']['name'] = trim($location['name']);
    }
    if (isset($location['latitude']) !== isset($location['longitude'])) {
      return moby_error(422, 'validation_failed', t('The latitude and longitude must be provided together.'), isset($location['latitude']) ? 'location.longitude' : 'location.latitude');
    }
    foreach (array('latitude' => 90, 'longitude' => 180) as $coordinate => $limit) {
      if (isset($location[$coordinate])) {
        if ((!is_int($location[$coordinate]) && !is_float($location[$coordinate])) || abs($location[$coordinate]) > $limit) {
          return moby_error(422, 'validation_failed', t('The @coordinate must be a number between -@limit and @limit.', array('@coordinate' => $coordinate, '@limit' => $limit)), 'location.' . $coordinate);
        }
        $values['location'][$coordinate] = (float) $location[$coordinate];
      }
    }
  }

  // Tags: the list, plus the #hashtags in the description. When updating, the
  // list replaces the current tags. If only the description changes, the
  // tags of hashtags that were removed from it are removed too.
  if (isset($data['tags'])) {
    if (!is_array($data['tags']) || ($data['tags'] && array_keys($data['tags']) !== range(0, count($data['tags']) - 1))) {
      return moby_error(422, 'validation_failed', t('The tags must be a list of strings.'), 'tags');
    }
    foreach ($data['tags'] as $delta => $tag) {
      if (!is_string($tag) || backdrop_strlen(trim($tag)) > MOBY_MAX_TAG_LENGTH) {
        return moby_error(422, 'validation_failed', t('Each tag must be a string of at most @max characters.', array('@max' => MOBY_MAX_TAG_LENGTH)), 'tags[' . $delta . ']');
      }
    }
  }
  if (!$is_update || isset($data['tags']) || isset($values['description'])) {
    if (isset($data['tags'])) {
      $tags = $data['tags'];
    }
    elseif ($is_update) {
      $removed = array_map('backdrop_strtolower', moby_hashtags(moby_post_description($node)));
      $tags = array_filter(moby_post_tags($node), function ($tag) use ($removed) {
        return !in_array(backdrop_strtolower($tag), $removed, TRUE);
      });
    }
    else {
      $tags = array();
    }
    $description = isset($values['description']) ? $values['description'] : ($is_update ? moby_post_description($node) : '');
    $values['tags'] = moby_tags_clean(array_merge($tags, moby_hashtags($description)));
    if (count($values['tags']) > MOBY_MAX_TAGS_PER_POST) {
      return moby_error(422, 'validation_failed', t('A post may have at most @max tags, including hashtags in the description.', array('@max' => MOBY_MAX_TAGS_PER_POST)), 'tags');
    }
  }

  // Media. When updating, items already on the post may be kept, whoever
  // uploaded them, including media added on the site rather than through the
  // API.
  $current_media = $is_update ? moby_post_media_items($node) : array();
  if (!$is_update) {
    $values['media'] = array();
  }
  if (isset($data['media'])) {
    $values['media'] = array();
    if (!is_array($data['media']) || ($data['media'] && array_keys($data['media']) !== range(0, count($data['media']) - 1))) {
      return moby_error(422, 'validation_failed', t('The media must be a list.'), 'media');
    }
    if ($data['media'] && !moby_media_field()) {
      return moby_error(422, 'validation_failed', t('This site does not accept photos or videos.'), 'media');
    }
    // A post already over the limit, from the site, can still be edited.
    $max_media = max(moby_max_media_per_post(), count($current_media));
    if (count($data['media']) > $max_media) {
      return moby_error(422, 'validation_failed', format_plural($max_media, 'A post may have at most 1 media item.', 'A post may have at most @count media items.'), 'media');
    }
    foreach ($data['media'] as $delta => $item) {
      $field = 'media[' . $delta . ']';
      if (!is_array($item) || !isset($item['id']) || !is_int($item['id'])) {
        return moby_error(422, 'validation_failed', t('Each media item must be an object with a numeric id.'), $field);
      }
      if (isset($values['media'][$item['id']])) {
        return moby_error(422, 'validation_failed', t('Media item @id is included more than once.', array('@id' => $item['id'])), $field);
      }
      if (isset($current_media[$item['id']])) {
        $alt = (string) $current_media[$item['id']]['description'];
      }
      else {
        $media = moby_media_load($item['id']);
        if (!$media || $media['file']->uid != $account->uid) {
          return moby_error(422, 'validation_failed', t('Media item @id does not exist. It may have expired; upload it again.', array('@id' => $item['id'])), $field);
        }
        if ($media['file']->status == FILE_STATUS_PERMANENT) {
          return moby_error(422, 'validation_failed', t('Media item @id is already attached to a post.', array('@id' => $item['id'])), $field);
        }
        $alt = $media['alt'];
      }
      if (isset($item['alt'])) {
        if (!is_string($item['alt']) || backdrop_strlen($item['alt']) > MOBY_MAX_ALT_LENGTH) {
          return moby_error(422, 'validation_failed', t('Alt text must be a string of at most @max characters.', array('@max' => MOBY_MAX_ALT_LENGTH)), $field . '.alt');
        }
        $alt = trim($item['alt']);
      }
      $values['media'][$item['id']] = array(
        'fid' => $item['id'],
        'alt' => $alt,
        'display' => isset($current_media[$item['id']]['display']) ? $current_media[$item['id']]['display'] : 1,
      );
    }
    $values['media'] = array_values($values['media']);
  }

  if ($is_update) {
    // Leave out what can't be saved. As on the node form, fields the user may
    // not edit keep their values.
    $mapping = config_get('moby.settings', 'mapping');
    $editable = function ($field_name) use ($node) {
      $field_name = _moby_mapped_field($field_name);
      return $field_name && field_access('edit', field_info_field($field_name), 'node', $node);
    };
    if (!$editable($mapping['description']['field'])) {
      unset($values['description']);
    }
    if ($mapping['date']['field'] !== 'created' && !$editable($mapping['date']['field'])) {
      unset($values['timestamp']);
    }
    if (!$editable($mapping['media']['field'])) {
      unset($values['media']);
    }
    if (!moby_tags_field() || !field_access('edit', field_info_field(moby_tags_field()), 'node', $node)) {
      unset($values['tags']);
    }
    if (isset($values['location'])) {
      if (!$editable($mapping['location']['name_field'])) {
        unset($values['location']['name']);
      }
      if (!moby_coordinates_field() || !$editable($mapping['location']['coordinates_field'])) {
        unset($values['location']['latitude'], $values['location']['longitude']);
      }
      if (!$values['location']) {
        unset($values['location']);
      }
    }
  }

  // A post must still have something in it after the changes.
  $description = isset($values['description']) ? $values['description'] : ($is_update ? moby_post_description($node) : '');
  $media_count = isset($values['media']) ? count($values['media']) : count($current_media);
  if ($description === '' && !$media_count) {
    return moby_error(422, 'validation_failed', t('A post must have a description or at least one media item.'), 'description');
  }

  // Status. Users who may not choose the published status on the node form
  // get the content type's default, but may always save a draft.
  if (isset($data['status']) && !in_array($data['status'], array('published', 'draft'), TRUE)) {
    return moby_error(422, 'validation_failed', t('The status must be "published" or "draft".'), 'status');
  }
  if ($is_update) {
    if (isset($data['status'])) {
      $status = (int) ($data['status'] === 'published');
      // Other users may not publish a draft on a content type whose posts
      // aren't published by default, which would skip its review.
      if ($status && !$node->status && !moby_user_can_publish($account)) {
        return moby_error(403, 'forbidden', t('You do not have permission to publish this post.'), 'status');
      }
      $values['status'] = $status;
    }
  }
  elseif (user_access('administer nodes', $account)) {
    $values['status'] = isset($data['status']) ? (int) ($data['status'] === 'published') : (int) config_get('moby.settings', 'default_status');
  }
  else {
    $node_type = node_type_get_type(config_get('moby.settings', 'node_type'));
    $values['status'] = (isset($data['status']) && $data['status'] === 'draft') ? 0 : (int) $node_type->settings['status_default'];
  }

  return $values;
}

/**
 * Parses an ISO 8601 date and time with a UTC offset.
 *
 * The offset is required, as "Z" or as hours with optional minutes ("-06:00",
 * "-0600", or "-06"), so the time is unambiguous. Only the resulting moment
 * is kept; the offset itself is not stored.
 *
 * @param mixed $value
 *   The value from the request, such as "2026-10-27T18:30:00-06:00".
 *
 * @return int|false
 *   The Unix timestamp, or FALSE if the value is not valid.
 */
function moby_parse_date($value) {
  if (!is_string($value) || !preg_match('/^\d{4}-\d{2}-\d{2}T\d{2}:\d{2}(:\d{2}(\.\d+)?)?(Z|[+-]\d{2}(:?\d{2})?)$/', $value)) {
    return FALSE;
  }
  try {
    $date = new DateTime($value);
  }
  catch (Exception $e) {
    return FALSE;
  }
  // Reject dates that PHP silently corrects, such as February 30.
  $errors = DateTime::getLastErrors();
  if (!empty($errors['warning_count'])) {
    return FALSE;
  }
  return $date->getTimestamp();
}

/**
 * Applies validated post values to a node through the configured mapping.
 *
 * Only the values provided are changed. Mapped fields for other values keep
 * their contents.
 *
 * @param Node $node
 *   The node to update.
 * @param array $values
 *   Values from moby_post_validate(), and optionally the node's "title".
 */
function moby_post_apply($node, array $values) {
  $config = config('moby.settings');
  $mapping = $config->get('mapping');

  if (isset($values['status'])) {
    $node->status = $values['status'];
  }
  if (isset($values['title'])) {
    $node->title = $values['title'];
  }

  if (isset($values['description']) && ($field_name = _moby_mapped_field($mapping['description']['field']))) {
    $item = array('value' => $values['description']);
    $instance = field_info_instance('node', $field_name, $node->type);
    if (!empty($instance['settings']['text_processing'])) {
      $item['format'] = $mapping['description']['format'];
    }
    $node->{$field_name}[LANGUAGE_NONE] = ($values['description'] === '') ? array() : array($item);
  }

  $date_field = $mapping['date']['field'];
  if (!isset($values['timestamp'])) {
    // The date is not changing.
  }
  elseif ($date_field === 'created') {
    $node->created = $values['timestamp'];
  }
  elseif ($field_name = _moby_mapped_field($date_field)) {
    // Date fields store UTC values, in a format that depends on the type.
    $field = field_info_field($field_name);
    $formats = array('date' => 'Y-m-d\TH:i:s', 'datetime' => 'Y-m-d H:i:s');
    $value = isset($formats[$field['type']]) ? gmdate($formats[$field['type']], $values['timestamp']) : $values['timestamp'];
    $node->{$field_name}[LANGUAGE_NONE] = array(array('value' => $value));
  }

  if (isset($values['location']) && array_key_exists('name', $values['location']) && ($field_name = _moby_mapped_field($mapping['location']['name_field']))) {
    $name = $values['location']['name'];
    $node->{$field_name}[LANGUAGE_NONE] = (isset($name) && $name !== '') ? array(array('value' => $name)) : array();
  }
  if (isset($values['location']) && array_key_exists('latitude', $values['location']) && ($field_name = moby_coordinates_field())) {
    $items = array();
    if (isset($values['location']['latitude'])) {
      // Geofield computes its other columns from this. number_format() avoids
      // exponents, which WKT parsing would not accept for very small values.
      $items[] = array(
        'geom' => 'POINT (' . number_format($values['location']['longitude'], 12, '.', '') . ' ' . number_format($values['location']['latitude'], 12, '.', '') . ')',
        'input_format' => 'wkt',
      );
    }
    $node->{$field_name}[LANGUAGE_NONE] = $items;
  }

  if (isset($values['tags']) && ($vocabulary = moby_tags_vocabulary()) && ($tags_field = moby_tags_field())) {
    $items = array();
    foreach (moby_tags_term_ids($values['tags'], $vocabulary) as $tid) {
      $items[] = array('tid' => $tid);
    }
    $node->{$tags_field}[LANGUAGE_NONE] = $items;
  }

  if (isset($values['media']) && ($media_field = moby_media_field())) {
    $items = array();
    foreach ($values['media'] as $item) {
      $items[] = array(
        'fid' => $item['fid'],
        'display' => $item['display'],
        'description' => $item['alt'],
      );
    }
    $node->{$media_field['field']['field_name']}[LANGUAGE_NONE] = $items;
  }
}

/**
 * Returns a mapped field name, if the field exists on the configured type.
 *
 * @param string $field_name
 *   The field name from the mapping, which may be empty.
 *
 * @return string|false
 *   The field name, or FALSE if it is not mapped or no longer exists.
 */
function _moby_mapped_field($field_name) {
  if ($field_name && field_info_instance('node', $field_name, config_get('moby.settings', 'node_type'))) {
    return $field_name;
  }
  return FALSE;
}

/**
 * Generates a node title with the site's settings.
 *
 * @param string $description
 *   The post description.
 * @param int $timestamp
 *   The post date.
 *
 * @return string
 *   The title.
 */
function moby_post_generate_title($description, $timestamp) {
  $description_format = moby_description_format();
  return moby_post_title($description, $timestamp, config_get('moby.settings', 'title_length'), $description_format['html']);
}

/**
 * Returns the names of a post's tags, regardless of field access.
 *
 * @param Node $node
 *   The post's node.
 *
 * @return string[]
 *   The tag names, in order.
 */
function moby_post_tags($node) {
  $field_name = moby_tags_field();
  $items = $field_name ? field_get_items('node', $node, $field_name) : FALSE;
  if (!$items) {
    return array();
  }
  $terms = taxonomy_term_load_multiple(array_column($items, 'tid'));
  $tags = array();
  foreach ($items as $item) {
    if (isset($terms[$item['tid']])) {
      $tags[] = $terms[$item['tid']]->name;
    }
  }
  return $tags;
}

/**
 * Returns a post's stored description, regardless of field access.
 *
 * @param Node $node
 *   The post's node.
 *
 * @return string
 *   The description, or an empty string.
 */
function moby_post_description($node) {
  $field_name = _moby_mapped_field(config_get('moby.settings', 'mapping.description.field'));
  $items = $field_name ? field_get_items('node', $node, $field_name) : FALSE;
  return isset($items[0]['value']) ? (string) $items[0]['value'] : '';
}

/**
 * Returns a post's date, regardless of field access.
 *
 * @param Node $node
 *   The post's node.
 *
 * @return int
 *   The date as a Unix timestamp.
 */
function moby_post_timestamp($node) {
  $field_name = config_get('moby.settings', 'mapping.date.field');
  if ($field_name !== 'created' && ($field_name = _moby_mapped_field($field_name))) {
    $items = field_get_items('node', $node, $field_name);
    if (!empty($items[0]['value'])) {
      $value = $items[0]['value'];
      return is_numeric($value) ? (int) $value : strtotime($value . ' UTC');
    }
  }
  return (int) $node->created;
}

/**
 * Returns a post's media field items, regardless of field access.
 *
 * @param Node $node
 *   The post's node.
 *
 * @return array
 *   File field items, keyed by file ID.
 */
function moby_post_media_items($node) {
  $media_field = moby_media_field();
  $items = $media_field ? field_get_items('node', $node, $media_field['field']['field_name']) : FALSE;
  $media = array();
  foreach ($items ?: array() as $item) {
    $item += array('description' => '');
    $media[$item['fid']] = $item;
  }
  return $media;
}

/**
 * Returns the field that stores location coordinates.
 *
 * @return string|false
 *   The name of the mapped Geofield, or FALSE if the optional Geofield module
 *   is not enabled, no field is mapped, or the field is not a Geofield.
 */
function moby_coordinates_field() {
  if (!module_exists('geofield')) {
    return FALSE;
  }
  $field_name = _moby_mapped_field(config_get('moby.settings', 'mapping.location.coordinates_field'));
  if (!$field_name) {
    return FALSE;
  }
  $field = field_info_field($field_name);
  return $field['type'] === 'geofield' ? $field_name : FALSE;
}

/**
 * Generates a node title for a post.
 *
 * Posts do not have titles in the API, so the title is the start of the
 * description, or the date if there is no description.
 *
 * @param string $description
 *   The post description.
 * @param int $timestamp
 *   The post date.
 * @param int $length
 *   The maximum title length.
 * @param bool $is_html
 *   Whether the description is HTML, which is converted to plain text, as
 *   node titles are plain text.
 */
function moby_post_title($description, $timestamp, $length, $is_html) {
  if ($is_html) {
    // Replace tags with spaces, so paragraphs and line breaks separate words.
    $description = decode_entities(strip_tags(preg_replace('/</', ' <', $description)));
  }
  $text = trim(preg_replace('/\s+/u', ' ', $description));
  if ($text === '') {
    return t('Post from @date', array('@date' => format_date($timestamp, 'medium')));
  }
  return truncate_utf8($text, min((int) $length, 255), TRUE, TRUE);
}

/**
 * Converts a field validation exception into an API error response.
 *
 * @param FieldValidationException $e
 *   The exception thrown by field_attach_validate().
 */
function moby_post_field_error(FieldValidationException $e) {
  $mapping = config_get('moby.settings', 'mapping');
  $properties = array(
    $mapping['description']['field'] => 'description',
    $mapping['date']['field'] => 'date',
    $mapping['location']['name_field'] => 'location.name',
    $mapping['location']['coordinates_field'] => 'location',
    $mapping['media']['field'] => 'media',
  );
  foreach ($e->errors as $field_name => $languages) {
    foreach ($languages as $deltas) {
      foreach ($deltas as $errors) {
        $error = reset($errors);
        $property = isset($properties[$field_name]) ? $properties[$field_name] : NULL;
        return moby_error(422, 'validation_failed', trim(decode_entities(strip_tags($error['message']))), $property);
      }
    }
  }
  return moby_error(422, 'validation_failed', t('The post is not valid.'));
}

/**
 * Loads a post that the current user may view.
 *
 * Any node of the configured content type is a post, whether or not it was
 * created through the API.
 *
 * @param int $nid
 *   The node ID.
 *
 * @return Node|false
 *   The post's node, or FALSE if it does not exist, is not of the configured
 *   content type, or the user may not view it.
 */
function moby_post_load($nid) {
  $node = node_load($nid);
  if (!$node || $node->type !== config_get('moby.settings', 'node_type') || !node_access('view', $node)) {
    return FALSE;
  }
  return $node;
}

/**
 * Loads a post created through the API by its client ID.
 *
 * @param int $uid
 *   The user who created the post.
 * @param string $client_id
 *   The client ID sent when the post was created.
 *
 * @return Node|false
 *   The post's node, or FALSE if there is none.
 */
function moby_post_load_by_client_id($uid, $client_id) {
  $nid = db_query('SELECT nid FROM {moby_post} WHERE uid = :uid AND client_id = :client_id', array(':uid' => $uid, ':client_id' => $client_id))->fetchField();
  return $nid ? node_load($nid) : FALSE;
}

/**
 * Returns the API representation of a post.
 *
 * @param Node $node
 *   The post's node.
 */
function moby_post_data($node) {
  $mapping = config_get('moby.settings', 'mapping');
  // Fields the user may not view, such as with field permissions, are
  // treated as empty.
  $field_items = function ($field_name) use ($node) {
    $field_name = _moby_mapped_field($field_name);
    if (!$field_name || !field_access('view', field_info_field($field_name), 'node', $node)) {
      return FALSE;
    }
    return field_get_items('node', $node, $field_name);
  };
  $field_value = function ($field_name) use ($field_items) {
    $items = $field_items($field_name);
    return isset($items[0]['value']) ? $items[0]['value'] : NULL;
  };

  // A date field the user may not view falls back to the authored date.
  $date_field = _moby_mapped_field($mapping['date']['field']);
  $timestamp = (!$date_field || field_access('view', field_info_field($date_field), 'node', $node)) ? moby_post_timestamp($node) : (int) $node->created;

  // Location.
  $location = array(
    'name' => $field_value($mapping['location']['name_field']),
    'latitude' => NULL,
    'longitude' => NULL,
  );
  // Geofield's "lat" and "lon" columns are the point itself, or the center of
  // other shapes entered on the site.
  $coordinates = moby_coordinates_field() ? $field_items($mapping['location']['coordinates_field']) : FALSE;
  if (isset($coordinates[0]['lat'], $coordinates[0]['lon'])) {
    $location['latitude'] = (float) $coordinates[0]['lat'];
    $location['longitude'] = (float) $coordinates[0]['lon'];
  }
  if (count(array_filter($location, 'is_null')) === count($location)) {
    $location = NULL;
  }

  // Media, with alt text from the field item's description.
  $media = array();
  if ($items = $field_items($mapping['media']['field'])) {
    $files = file_load_multiple(array_column($items, 'fid'));
    foreach ($items as $item) {
      if (isset($files[$item['fid']])) {
        $media[] = moby_media_data($files[$item['fid']], isset($item['description']) ? $item['description'] : '');
      }
    }
  }

  return array(
    'id' => (int) $node->nid,
    'url' => url('node/' . $node->nid, array('absolute' => TRUE)),
    'api_url' => url(MOBY_API_PATH . '/posts/' . $node->nid, array('absolute' => TRUE)),
    'status' => $node->status ? 'published' : 'draft',
    'description' => (string) $field_value($mapping['description']['field']),
    'date' => format_date($timestamp, 'custom', 'c'),
    'location' => $location,
    'media' => $media,
    'tags' => ($tags_field = moby_tags_field()) && field_access('view', field_info_field($tags_field), 'node', $node) ? moby_post_tags($node) : array(),
    'created' => format_date($node->created, 'custom', 'c'),
    'changed' => format_date($node->changed, 'custom', 'c'),
    'permissions' => array(
      'edit' => node_access('update', $node),
      'delete' => node_access('delete', $node),
    ),
  );
}
