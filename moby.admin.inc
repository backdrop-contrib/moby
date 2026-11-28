<?php
/**
 * @file
 * Administrative settings and the setup wizard for Moby.
 *
 * The settings form and the wizard are built from the same sections, each with
 * its own validate and submit handlers:
 *
 * - General: moby_settings_form_general().
 * - Uploads: moby_settings_form_uploads().
 * - Content type: moby_settings_form_content_type().
 * - Video: moby_settings_form_video().
 * - Location: moby_settings_form_location().
 * - Tags: moby_settings_form_tags().
 *
 * Sections take a $form_type of "config" for the settings form or "wizard" for
 * the setup wizard, where the two need to differ.
 */

/**
 * The upload size recommended for sites with video content, in bytes.
 */
define('MOBY_RECOMMENDED_UPLOAD_SIZE', 512 * 1024 * 1024);

/**
 * The value of a field option that creates a new field when submitted.
 */
define('MOBY_CREATE_FIELD', '_create');

/**
 * Form constructor for the Moby settings form.
 *
 * Maps the standardized REST API properties onto a node type and its fields.
 */
function moby_settings_form($form, &$form_state) {
  if (!config_get('moby.settings', 'setup_complete') && $_SERVER['REQUEST_METHOD'] === 'GET') {
    backdrop_set_message(t('The setup wizard has not been run yet. <a href="@url">Run the setup wizard</a> to get Moby ready to post.', array('@url' => url('admin/config/services/moby/setup'))), 'warning');
  }

  $form += moby_settings_form_content_type($form_state, 'config');
  $form += moby_settings_form_video($form_state, 'config');
  $form += moby_settings_form_uploads('config');
  $form += moby_settings_form_general('config');

  $form['#validate'] = array(
    'moby_settings_form_video_validate',
  );
  $form['#submit'] = array(
    'moby_settings_form_content_type_submit',
    'moby_settings_form_video_submit',
    'moby_settings_form_location_submit',
    'moby_settings_form_tags_submit',
    'moby_settings_form_uploads_submit',
    'moby_settings_form_general_submit',
    'moby_settings_form_saved_submit',
  );
  $form['actions']['#type'] = 'actions';
  $form['actions']['submit'] = array(
    '#type' => 'submit',
    '#value' => t('Save configuration'),
  );
  return $form;
}

/**
 * Form submission handler: Reports that the settings form was saved.
 */
function moby_settings_form_saved_submit($form, &$form_state) {
  backdrop_set_message(t('The configuration options have been saved.'));
}

/**
 * Returns the content type being configured in a form.
 *
 * This may differ from the saved one when the form is rebuilt by the content
 * type's #ajax. Changing the content type skips validation, so its value is
 * read from the input. Any value other than an existing content type results in
 * no mapping being shown.
 *
 * @param array $form_state
 *   The form state.
 * @return string
 *   The content type's machine name, or an empty string.
 */
function _moby_settings_form_node_type(array &$form_state) {
  if (isset($form_state['triggering_element']['#name']) && $form_state['triggering_element']['#name'] === 'node_type') {
    $node_type = is_string($form_state['input']['node_type']) ? $form_state['input']['node_type'] : '';
    // Discard the submitted mapping for the previous content type, so the
    // defaults for the new content type are used instead. Other sections of
    // the form don't depend on the content type.
    unset($form_state['input']['mapping']['description']);
    unset($form_state['input']['mapping']['date']);
    unset($form_state['input']['mapping']['location']);
    unset($form_state['input']['mapping']['media']['field']);
    return $node_type;
  }
  if (isset($form_state['values']['node_type'])) {
    return $form_state['values']['node_type'];
  }
  return (string) config_get('moby.settings', 'node_type');
}

/**
 * Returns the content type section of the form.
 *
 * @param array $form_state
 *   The form state.
 * @param string $form_type
 *   Either "config" or "wizard". The settings form includes the location and
 *   tags, which the wizard asks for in later steps.
 *
 * @return array
 *   Form elements.
 */
function moby_settings_form_content_type(array &$form_state, $form_type) {
  $node_type = _moby_settings_form_node_type($form_state);
  $mapping = config_get('moby.settings', 'mapping');

  $form['node_type'] = array(
    '#type' => 'select',
    '#title' => t('Content type'),
    '#description' => t('Posts created through the API will be saved as this content type.'),
    '#options' => node_type_get_names(),
    '#empty_option' => t('- Select a content type -'),
    '#default_value' => $node_type,
    // In the wizard, this depends on whether a new content type is created.
    '#required' => $form_type === 'config',
    '#ajax' => array(
      'callback' => 'moby_settings_form_mapping_ajax',
      'wrapper' => 'moby-mapping-wrapper',
    ),
    // Do not validate the form when only the content type has changed.
    '#limit_validation_errors' => array(),
  );

  // The mapping is always wrapped in a container, even when empty, so it can
  // be replaced when the content type changes.
  $form['mapping_wrapper'] = array(
    '#type' => 'container',
    // #states finds the element by its #id, and #ajax by its ID attribute.
    '#id' => 'moby-mapping-wrapper',
    '#attributes' => array('id' => 'moby-mapping-wrapper'),
  );
  if ($node_type && node_type_get_type($node_type)) {
    $form['mapping_wrapper']['mapping'] = array(
      '#type' => 'fieldset',
      '#title' => t('Field mapping'),
      '#description' => t('Choose where each API property is stored. Properties that are not mapped are ignored.'),
      '#tree' => TRUE,
    );
    $form['mapping_wrapper']['mapping'] += _moby_settings_form_mapping($node_type, $mapping);
    // Location and tags get fieldsets of their own, after the field mapping.
    // Their elements set #parents, so they're still saved in "mapping".
    if ($form_type === 'config') {
      $form['mapping_wrapper']['location'] = array(
        '#type' => 'fieldset',
        '#title' => t('Location'),
      ) + moby_settings_form_location($node_type, $form_type);
      $form['mapping_wrapper']['tags'] = array(
        '#type' => 'fieldset',
        '#title' => t('Tags'),
      ) + moby_settings_form_tags($form_type);
    }
  }
  return $form;
}

/**
 * AJAX callback: Returns the field mapping for the selected content type.
 */
function moby_settings_form_mapping_ajax($form, $form_state) {
  return $form['mapping_wrapper'];
}

/**
 * Form submission handler: Saves the content type section.
 */
function moby_settings_form_content_type_submit($form, &$form_state) {
  $values = $form_state['values'];
  $config = config('moby.settings');
  $config->set('node_type', $values['node_type']);
  if (isset($values['mapping']['description'])) {
    $config->set('mapping.description', $values['mapping']['description']);
  }
  if (isset($values['mapping']['date'])) {
    $config->set('mapping.date', $values['mapping']['date']);
  }
  if (isset($values['mapping']['location']['name_field'])) {
    $config->set('mapping.location.name_field', $values['mapping']['location']['name_field']);
  }
  if (isset($values['mapping']['media']['field'])) {
    $config->set('mapping.media.field', $values['mapping']['media']['field']);
  }
  $config->save();

  // A new content type may not have a field for tags yet.
  _moby_settings_form_ensure_tags_field();
}

/**
 * Builds the field mapping elements for a content type.
 *
 * @param string $node_type
 *   The content type machine name.
 * @param array $mapping
 *   The saved mapping from moby.settings. Saved fields that do not exist on
 *   this content type are not used as defaults.
 *
 * @return array
 *   Form elements, structured like the "mapping" setting.
 */
function _moby_settings_form_mapping($node_type, array $mapping) {
  $form = array();

  $form['description']['field'] = array(
    '#type' => 'select',
    '#title' => t('Description'),
    '#options' => _moby_field_options($node_type, array('text', 'text_long', 'text_with_summary')),
    '#empty_option' => t('- Not mapped -'),
    '#default_value' => $mapping['description']['field'],
  );
  $format_options = array();
  foreach (filter_formats() as $format) {
    $format_options[$format->format] = $format->name;
  }
  $form['description']['format'] = array(
    '#type' => 'select',
    '#title' => t('Description text format'),
    '#description' => t('Used when the description field supports text formats. Line breaks and URLs in the description should be handled by this format.'),
    '#options' => $format_options,
    '#default_value' => $mapping['description']['format'],
  );

  $form['date']['field'] = array(
    '#type' => 'select',
    '#title' => t('Date'),
    '#options' => array('created' => t('Authored on date')) + _moby_field_options($node_type, array('date', 'datestamp', 'datetime')),
    '#default_value' => $mapping['date']['field'],
  );

  $form['media']['field'] = array(
    '#type' => 'select',
    '#title' => t('Media field'),
    '#description' => t('A file field that accepts multiple values. Photos and videos are stored together in the order provided. The alt text of each item is saved as the file description.'),
    '#options' => _moby_field_options($node_type, array('file'), TRUE),
    '#empty_option' => t('- Not mapped -'),
    '#default_value' => $mapping['media']['field'],
  );

  // Do not default to saved fields that are not on this content type.
  $field_parents = array(
    array('description', 'field'),
    array('date', 'field'),
    array('media', 'field'),
  );
  foreach ($field_parents as $parents) {
    $element = &backdrop_array_get_nested_value($form, $parents);
    if (!isset($element['#options'][$element['#default_value']])) {
      $element['#default_value'] = isset($element['#empty_option']) ? '' : key($element['#options']);
    }
    unset($element);
  }

  return $form;
}

/**
 * Returns the location section of the form: the name and coordinates.
 *
 * @param string $node_type
 *   The content type machine name.
 * @param string $form_type
 *   Either "config" or "wizard". The wizard also offers to create a field.
 *
 * @return array
 *   Form elements, with #parents to save them in the "mapping" setting.
 */
function moby_settings_form_location($node_type, $form_type) {
  $saved_name = config_get('moby.settings', 'mapping.location.name_field');
  $name_options = _moby_field_options($node_type, array('text'));
  $form['name_field'] = array(
    '#type' => 'select',
    '#title' => t('Location name'),
    '#description' => t('A text field for the name of the place, such as "Boulder, Colorado".'),
    '#options' => $name_options,
    '#empty_option' => t('- Not mapped -'),
    // Do not default to a saved field that is not on this content type.
    '#default_value' => isset($name_options[$saved_name]) ? $saved_name : '',
    '#parents' => array('mapping', 'location', 'name_field'),
  );

  $saved = config_get('moby.settings', 'mapping.location.coordinates_field');
  $parents = array('mapping', 'location', 'coordinates_field');

  if (!module_exists('geofield')) {
    // Coordinates are not available, so there is nothing to choose. The saved
    // mapping is kept, in case Geofield is only temporarily disabled.
    $form['coordinates_field'] = array(
      '#type' => 'value',
      '#value' => $saved,
      '#parents' => $parents,
    );
    if ($form_type === 'config') {
      $form['coordinates_help'] = array(
        '#type' => 'help',
        '#markup' => t('Enable the <a href="@url">Geofield</a> module to also save coordinates.', array('@url' => 'https://backdropcms.org/project/geofield')),
      );
    }
    return $form;
  }

  $options = _moby_field_options($node_type, array('geofield'));
  $default = isset($options[$saved]) ? $saved : '';
  if ($form_type === 'wizard' && !isset($options['field_mobile_coordinates'])) {
    $options = array(MOBY_CREATE_FIELD => t('Create a new Coordinates field (field_mobile_coordinates)')) + $options;
    $default = $default ? $default : MOBY_CREATE_FIELD;
  }
  $form['coordinates_field'] = array(
    '#type' => 'select',
    '#title' => t('Location coordinates'),
    '#description' => t('A Geofield, which stores the latitude and longitude together.'),
    '#options' => $options,
    '#empty_option' => t('- Not mapped -'),
    '#default_value' => $default,
    '#parents' => $parents,
  );
  return $form;
}

/**
 * Form submission handler: Saves the location section.
 */
function moby_settings_form_location_submit($form, &$form_state) {
  if (isset($form_state['values']['mapping']['location']['name_field'])) {
    config_set('moby.settings', 'mapping.location.name_field', $form_state['values']['mapping']['location']['name_field']);
  }
  if (isset($form_state['values']['mapping']['location']['coordinates_field'])) {
    config_set('moby.settings', 'mapping.location.coordinates_field', $form_state['values']['mapping']['location']['coordinates_field']);
  }
}

/**
 * Returns the tags section of the form.
 *
 * @param string $form_type
 *   Either "config" or "wizard". The wizard defaults to the "tags" vocabulary
 *   when no other is selected.
 *
 * @return array
 *   Form elements, with #parents to save them in the "mapping" setting.
 */
function moby_settings_form_tags($form_type) {
  $parents = array('mapping', 'tags', 'vocabulary');
  $saved = (string) config_get('moby.settings', 'mapping.tags.vocabulary');

  if (!module_exists('taxonomy')) {
    // The saved vocabulary is kept, in case Taxonomy is only disabled for now.
    $form['vocabulary'] = array(
      '#type' => 'value',
      '#value' => $saved,
      '#parents' => $parents,
    );
    $form['help'] = array(
      '#type' => 'help',
      '#markup' => t('Enable the Taxonomy module to also save tags.'),
    );
    return $form;
  }

  $vocabularies = array();
  foreach (taxonomy_get_vocabularies() as $vocabulary) {
    $vocabularies[$vocabulary->machine_name] = $vocabulary->name;
  }
  $default = isset($vocabularies[$saved]) ? $saved : '';
  if ($form_type === 'wizard' && !$default && isset($vocabularies['tags'])) {
    $default = 'tags';
  }
  $form['vocabulary'] = array(
    '#type' => 'select',
    '#title' => t('Tag vocabulary'),
    '#description' => t('Tags added in the app, and #hashtags in descriptions, are saved as terms in this vocabulary. Terms that don\'t exist yet are added. If the content type has no field for these tags, one is added.'),
    '#options' => $vocabularies,
    '#empty_option' => t('- Not mapped -'),
    '#default_value' => $default,
    '#parents' => $parents,
  );
  return $form;
}

/**
 * Form submission handler: Saves the tags section.
 */
function moby_settings_form_tags_submit($form, &$form_state) {
  if (isset($form_state['values']['mapping']['tags']['vocabulary'])) {
    config_set('moby.settings', 'mapping.tags.vocabulary', $form_state['values']['mapping']['tags']['vocabulary']);
  }
  _moby_settings_form_ensure_tags_field();
}

/**
 * Adds a field for tags to the content type, if it needs one.
 */
function _moby_settings_form_ensure_tags_field() {
  $node_type = config_get('moby.settings', 'node_type');
  $vocabulary = moby_tags_vocabulary();
  if ($node_type && node_type_get_type($node_type) && $vocabulary && !moby_tags_field($node_type, $vocabulary)) {
    $field_name = moby_tags_ensure_field($node_type, $vocabulary);
    backdrop_set_message(t('The %field field has been added to the content type, to save tags in.', array('%field' => $field_name)));
  }
}

/**
 * Returns the video section of the form.
 *
 * @param array $form_state
 *   The form state.
 * @param string $form_type
 *   Either "config" or "wizard". The wizard only asks about videos, and
 *   recommends a file type.
 *
 * @return array
 *   Form elements, with #parents to save them in the "mapping" setting.
 */
function moby_settings_form_video(array &$form_state, $form_type) {
  $mapping = config_get('moby.settings', 'mapping');
  $file_types = array_map('check_plain', file_type_get_names());

  $form['video'] = array(
    '#type' => $form_type === 'config' ? 'fieldset' : 'container',
    '#title' => t('Photos and videos'),
  );
  if ($form_type === 'config') {
    $form['video']['image_file_type'] = array(
      '#type' => 'select',
      '#title' => t('Photo file type'),
      '#options' => $file_types,
      '#empty_option' => t('- Determine from MIME type -'),
      '#default_value' => $mapping['media']['file_types']['image'],
      '#parents' => array('mapping', 'media', 'file_types', 'image'),
    );
  }

  // The file type's #ajax skips validation, so its value is read from the
  // input. The thumbnail field's options depend on it.
  $input = isset($form_state['input']['mapping']['media']['file_types']['video']) ? $form_state['input']['mapping']['media']['file_types']['video'] : NULL;
  $video_type = is_string($input) ? $input : $mapping['media']['file_types']['video'];
  if (isset($form_state['triggering_element']['#name']) && $form_state['triggering_element']['#name'] === 'mapping[media][file_types][video]') {
    unset($form_state['input']['mapping']['media']['thumbnail_field']);
  }

  $form['video']['video_file_type'] = array(
    '#type' => 'select',
    '#title' => t('Video file type'),
    '#options' => $file_types,
    '#default_value' => $video_type,
    '#parents' => array('mapping', 'media', 'file_types', 'video'),
    '#ajax' => array(
      'callback' => 'moby_settings_form_video_ajax',
      'wrapper' => 'moby-thumbnail-wrapper',
    ),
    '#limit_validation_errors' => array(),
  );
  if ($form_type === 'config') {
    $form['video']['video_file_type']['#empty_option'] = t('- Determine from MIME type -');
  }
  elseif (isset($file_types['video'])) {
    $form['video']['video_file_type']['#options']['video'] = t('@label (recommended)', array('@label' => $file_types['video']));
  }

  $thumbnail_options = _moby_file_image_field_options(moby_video_file_type((string) $video_type));
  $form['video']['thumbnail_wrapper'] = array(
    '#type' => 'container',
    '#attributes' => array('id' => 'moby-thumbnail-wrapper'),
  );
  $form['video']['thumbnail_wrapper']['thumbnail_field'] = array(
    '#type' => 'select',
    '#title' => t('Video thumbnail field'),
    '#description' => t('An image field on the video file type, where the thumbnail the app sends with each video is saved. Videos have no thumbnail in the API if this is not set. Add an image field on the <a href="@url">file types</a> page to create one.', array('@url' => url('admin/structure/file-types'))),
    '#options' => $thumbnail_options,
    '#empty_option' => t('- Not mapped -'),
    '#default_value' => isset($thumbnail_options[$mapping['media']['thumbnail_field']]) ? $mapping['media']['thumbnail_field'] : '',
    '#parents' => array('mapping', 'media', 'thumbnail_field'),
  );
  return $form;
}

/**
 * AJAX callback: Returns the thumbnail field for the selected file type.
 */
function moby_settings_form_video_ajax($form, $form_state) {
  return $form['video']['thumbnail_wrapper'];
}

/**
 * Form validation handler: Validates the video section.
 *
 * The thumbnail field must be on the file type videos are given.
 */
function moby_settings_form_video_validate($form, &$form_state) {
  if (empty($form_state['values']['mapping']['media']['thumbnail_field'])) {
    return;
  }
  $field_name = $form_state['values']['mapping']['media']['thumbnail_field'];
  $file_type = moby_video_file_type((string) $form_state['values']['mapping']['media']['file_types']['video']);
  if (!$file_type || !field_info_instance('file', $field_name, $file_type)) {
    $file_types = file_type_get_names();
    form_set_error('mapping][media][thumbnail_field', t('The video thumbnail field must be attached to the %type file type.', array('%type' => isset($file_types[$file_type]) ? $file_types[$file_type] : t('video'))));
  }
}

/**
 * Form submission handler: Saves the video section.
 */
function moby_settings_form_video_submit($form, &$form_state) {
  $media = $form_state['values']['mapping']['media'];
  $config = config('moby.settings');
  if (isset($media['file_types']['image'])) {
    $config->set('mapping.media.file_types.image', $media['file_types']['image']);
  }
  $config->set('mapping.media.file_types.video', $media['file_types']['video']);
  $config->set('mapping.media.thumbnail_field', $media['thumbnail_field']);
  $config->save();
}

/**
 * Returns the uploads section of the form.
 *
 * @param string $form_type
 *   Either "config" or "wizard".
 *
 * @return array
 *   Form elements.
 */
function moby_settings_form_uploads($form_type) {
  $form['max_video_size'] = array(
    '#type' => 'textfield',
    '#title' => t('Maximum video upload size'),
    '#description' => t('Enter a value like "80 KB" or "50 MB" to restrict the size of uploaded videos. The app uses this limit to decide how much to compress videos before uploading. This may not exceed PHP\'s maximum upload size (currently <strong>%limit</strong>), which is used if left empty.', array('%limit' => format_size(file_upload_max_size()))),
    '#size' => 10,
    '#default_value' => config_get('moby.settings', 'max_video_size'),
    '#element_validate' => array('_moby_settings_form_validate_max_size'),
  );
  return $form;
}

/**
 * Form submission handler: Saves the uploads section.
 */
function moby_settings_form_uploads_submit($form, &$form_state) {
  config_set('moby.settings', 'max_video_size', $form_state['values']['max_video_size']);
}

/**
 * Returns the general section of the form.
 *
 * @param string $form_type
 *   Either "config" or "wizard".
 *
 * @return array
 *   Form elements.
 */
function moby_settings_form_general($form_type) {
  $config = config('moby.settings');
  $form['auth_mechanisms'] = moby_settings_form_auth_mechanisms();
  $form['default_status'] = array(
    '#type' => 'checkbox',
    '#title' => t('Publish posts by default'),
    '#description' => t('Used when a request does not specify a status.'),
    '#default_value' => $config->get('default_status'),
  );
  $form['title_length'] = array(
    '#type' => 'number',
    '#title' => t('Generated title length'),
    '#description' => t('Posts do not have titles in the API. The node title is generated by truncating the description to this many characters, or from the date when there is no description.'),
    '#min' => 10,
    '#max' => 255,
    '#default_value' => $config->get('title_length'),
  );
  $form['thumbnail_style'] = array(
    '#type' => 'select',
    '#title' => t('Thumbnail image style'),
    '#description' => t('The image style of the thumbnails apps show in place of full-size photos and video thumbnails, such as in lists of posts. Choose or add one on the <a href="@url">image styles</a> page.', array('@url' => url('admin/config/media/image-styles'))),
    '#options' => image_style_options(FALSE),
    '#default_value' => $config->get('thumbnail_style'),
    '#required' => TRUE,
  );
  return $form;
}

/**
 * Form submission handler: Saves the general section.
 */
function moby_settings_form_general_submit($form, &$form_state) {
  $values = $form_state['values'];
  $config = config('moby.settings');
  foreach (array('default_status', 'title_length', 'thumbnail_style') as $key) {
    $config->set($key, $values[$key]);
  }
  $names = moby_settings_form_auth_mechanisms_value($values['auth_mechanisms']);
  // The table is hidden when there is only one mechanism, which is then
  // always enabled.
  $all = array_keys(moby_get_auth_mechanisms());
  if (!$names && count($all) == 1) {
    $names = $all;
  }
  $config->set('auth_mechanisms', $names);
  $config->save();
}

/**
 * Returns the authentication mechanisms table of the general section.
 *
 * Each row has a checkbox to enable the mechanism and a weight, which the
 * table lets administrators drag to set the order. Enabled mechanisms that no
 * longer exist are left out, and are removed when the form is saved.
 */
function moby_settings_form_auth_mechanisms() {
  $mechanisms = moby_get_auth_mechanisms();
  $enabled = array_values(array_intersect(moby_auth_mechanism_names(), array_keys($mechanisms)));
  // Enabled mechanisms first, in their order, then the rest.
  $order = array_merge($enabled, array_diff(array_keys($mechanisms), $enabled));

  $element = array(
    '#type' => 'fieldset',
    '#title' => t('Logging in'),
    '#description' => t('How users can log in from the app. The app offers the first enabled way, with the others under "Log in a different way". Turning one off does not log out devices that used it.'),
    '#tree' => TRUE,
    '#theme' => 'moby_auth_mechanisms_table',
    '#element_validate' => array('moby_settings_form_auth_mechanisms_validate'),
    // Only shown when there is a choice.
    '#access' => count($mechanisms) > 1,
  );
  foreach ($order as $weight => $name) {
    $mechanism = moby_get_auth_mechanism($name);
    $description = isset($mechanisms[$name]['description']) ? $mechanisms[$name]['description'] : '';
    if ($mechanism && $mechanism->getMinApiVersion() > MOBY_API_MIN_VERSION) {
      $description .= ' ' . t('Apps from before API version @version do not offer it.', array('@version' => $mechanism->getMinApiVersion()));
    }
    $element[$name]['enabled'] = array(
      '#type' => 'checkbox',
      '#title' => check_plain($mechanisms[$name]['label']),
      '#description' => filter_xss_admin(trim($description)),
      '#default_value' => in_array($name, $enabled),
    );
    $element[$name]['weight'] = array(
      '#type' => 'weight',
      '#title' => t('Weight for @title', array('@title' => $mechanisms[$name]['label'])),
      '#title_display' => 'invisible',
      '#delta' => max(10, count($order)),
      '#default_value' => $weight,
      '#attributes' => array('class' => array('moby-auth-mechanism-weight')),
    );
  }
  return $element;
}

/**
 * Element validation handler: Requires an enabled authentication mechanism.
 */
function moby_settings_form_auth_mechanisms_validate($element, &$form_state) {
  if ($element['#access'] && !moby_settings_form_auth_mechanisms_value(backdrop_array_get_nested_value($form_state['values'], $element['#parents']))) {
    form_error($element, t('Enable at least one way of logging in.'));
  }
}

/**
 * Returns the enabled mechanisms' names, in order, from the table's values.
 *
 * @param array|null $values
 *   The table's values, keyed by mechanism. NULL when the table was hidden,
 *   which keeps the saved setting.
 */
function moby_settings_form_auth_mechanisms_value($values) {
  if (!is_array($values)) {
    return moby_auth_mechanism_names();
  }
  backdrop_sort($values, array('weight' => SORT_NUMERIC));
  return array_keys(array_filter($values, function ($row) {
    return !empty($row['enabled']);
  }));
}

/**
 * Returns HTML for the authentication mechanisms table, with draggable rows.
 *
 * @param array $variables
 *   An associative array containing:
 *   - element: The table's form element.
 *
 * @ingroup themeable
 */
function theme_moby_auth_mechanisms_table($variables) {
  $element = $variables['element'];
  $rows = array();
  foreach (element_children($element) as $name) {
    $rows[] = array(
      'data' => array(
        backdrop_render($element[$name]['enabled']),
        backdrop_render($element[$name]['weight']),
      ),
      'class' => array('draggable'),
    );
  }
  $id = 'moby-auth-mechanisms';
  backdrop_add_tabledrag($id, 'order', 'sibling', 'moby-auth-mechanism-weight');
  $table = theme('table', array(
    'header' => array(t('Way of logging in'), t('Weight')),
    'rows' => $rows,
    'attributes' => array('id' => $id),
  ));
  // The fieldset wraps the table, as the element's #theme_wrappers.
  return $table;
}

/**
 * Returns the steps of the setup wizard.
 *
 * @return array
 *   Step titles, keyed by the step's name in the path, in order.
 */
function moby_setup_wizard_steps() {
  return array(
    'content' => t('Content type'),
    'video' => t('Videos'),
    'location' => t('Location'),
    'tags' => t('Tags'),
    'uploads' => t('Upload size'),
    'permissions' => t('Permissions'),
    'done' => t('Done'),
  );
}

/**
 * Returns the path of a step of the setup wizard.
 *
 * @param string $step
 *   The step's name.
 * @param int $offset
 *   The number of steps after this one to return, or before if negative.
 *
 * @return string|null
 *   The path, or NULL if there is no such step.
 */
function moby_setup_wizard_path($step, $offset = 0) {
  $steps = array_keys(moby_setup_wizard_steps());
  $index = array_search($step, $steps) + $offset;
  return isset($steps[$index]) ? 'admin/config/services/moby/setup/' . $steps[$index] : NULL;
}

/**
 * Page callback: Shows a step of the setup wizard.
 *
 * @param string $step
 *   The step's name.
 */
function moby_setup_wizard_page($step = 'content') {
  $steps = moby_setup_wizard_steps();
  if (func_num_args() > 1 || !isset($steps[$step])) {
    return MENU_NOT_FOUND;
  }
  // Later steps depend on the content type.
  if ($step !== 'content' && !node_type_get_type((string) config_get('moby.settings', 'node_type'))) {
    backdrop_goto(moby_setup_wizard_path('content'));
  }
  // This isn't set in the form constructor, as forms with #ajax are cached.
  backdrop_set_title(t('Moby setup: @step', array('@step' => $steps[$step])));
  return backdrop_get_form('moby_setup_wizard_form', $step);
}

/**
 * Form constructor for the setup wizard.
 *
 * @param string $step
 *   The step's name.
 */
function moby_setup_wizard_form($form, &$form_state, $step) {
  $steps = moby_setup_wizard_steps();
  $form_state['moby_step'] = $step;

  $form['progress'] = array(
    '#type' => 'help',
    '#markup' => t('Step @number of @total', array('@number' => array_search($step, array_keys($steps)) + 1, '@total' => count($steps))),
    '#weight' => -100,
  );
  $form['actions'] = array('#type' => 'actions', '#weight' => 100);
  $form = call_user_func_array('moby_setup_wizard_form_' . $step, array($form, &$form_state));

  // The redirect is added last, so it runs after a step's own handlers.
  $form['#submit'][] = 'moby_setup_wizard_form_next_submit';
  // The last step has nothing to go back and change.
  if ($step !== 'done' && ($back = moby_setup_wizard_path($step, -1))) {
    $form['actions']['back'] = array(
      '#type' => 'link',
      '#title' => t('Back'),
      '#href' => $back,
      '#weight' => 10,
    );
  }
  return $form;
}

/**
 * Form submission handler: Moves on to the next step of the setup wizard.
 */
function moby_setup_wizard_form_next_submit($form, &$form_state) {
  $form_state['redirect'] = moby_setup_wizard_path($form_state['moby_step'], 1);
}

/**
 * Builds step 1 of the setup wizard: The content type.
 */
function moby_setup_wizard_form_content($form, &$form_state) {
  // A content type from an earlier run of the wizard is used like any other.
  $can_create = !node_type_get_type('mobile');
  $has_type = node_type_get_type((string) config_get('moby.settings', 'node_type'));

  $form['intro'] = array(
    '#markup' => '<p>' . t('Posts from the app are saved as nodes of a content type. Which one should be used?') . '</p>',
  );
  $options = array();
  if ($can_create) {
    $options['new'] = t('Create a new "Mobile Post" content type, with fields for everything the app sends (recommended)');
  }
  $options['existing'] = t('Use an existing content type');
  $form['content_type_mode'] = array(
    '#type' => 'radios',
    '#title' => t('Content type'),
    '#title_display' => 'invisible',
    '#options' => $options,
    '#default_value' => ($can_create && !$has_type) ? 'new' : 'existing',
  );

  $form += moby_settings_form_content_type($form_state, 'wizard');
  $existing = array('visible' => array(':input[name="content_type_mode"]' => array('value' => 'existing')));
  $form['node_type']['#states'] = $existing;
  $form['mapping_wrapper']['#states'] = $existing;

  $form['#validate'][] = 'moby_setup_wizard_form_content_validate';
  $form['#submit'][] = 'moby_setup_wizard_form_content_submit';
  $form['#submit'][] = 'moby_settings_form_content_type_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Form validation handler for step 1 of the setup wizard.
 */
function moby_setup_wizard_form_content_validate($form, &$form_state) {
  if ($form_state['values']['content_type_mode'] === 'existing') {
    if (empty($form_state['values']['node_type'])) {
      form_set_error('node_type', t('Select a content type.'));
    }
    return;
  }
  $fields = array(
    'field_mobile_description' => 'text_long',
    'field_mobile_location' => 'text',
    'field_mobile_media' => 'file',
  );
  if (module_exists('date')) {
    $fields['field_mobile_date'] = 'datestamp';
  }
  foreach ($fields as $field_name => $type) {
    _moby_setup_validate_new_field($field_name, $type, 'content_type_mode');
  }
}

/**
 * Sets a form error if a field to be created exists with another field type.
 *
 * @param string $field_name
 *   The name of the field to be created.
 * @param string $type
 *   The field type it needs.
 * @param string $element
 *   The name of the form element to set the error on.
 */
function _moby_setup_validate_new_field($field_name, $type, $element) {
  $field = field_info_field($field_name);
  if ($field && $field['type'] !== $type) {
    form_set_error($element, t('The field %field already exists, but it is not the type needed here. Choose another option, or remove that field first.', array('%field' => $field_name)));
  }
}

/**
 * Form submission handler for step 1 of the setup wizard.
 *
 * Creates the new content type, if chosen, then sets the form's values so that
 * moby_settings_form_content_type_submit() saves it like any other.
 */
function moby_setup_wizard_form_content_submit($form, &$form_state) {
  if ($form_state['values']['content_type_mode'] === 'new') {
    $form_state['values']['node_type'] = 'mobile';
    $form_state['values']['mapping'] = _moby_setup_create_content_type();
    backdrop_set_message(t('The Mobile Post content type has been created.'));
  }

  // A coordinates field from another content type no longer applies.
  $coordinates = config_get('moby.settings', 'mapping.location.coordinates_field');
  if ($coordinates && !field_info_instance('node', $coordinates, $form_state['values']['node_type'])) {
    config_set('moby.settings', 'mapping.location.coordinates_field', '');
  }
}

/**
 * Creates the Mobile Post content type and its fields.
 *
 * @return array
 *   The new mapping for moby.settings. Coordinates are not included, as they are
 *   set up in a later step.
 */
function _moby_setup_create_content_type() {
  $type = node_type_set_defaults(array(
    'type' => 'mobile',
    'name' => 'Mobile Post',
    'base' => 'node_content',
    'module' => 'node',
    'description' => t('A post with photos, videos, and a location, created from the Moby app.'),
    'custom' => 1,
    'modified' => 1,
    'locked' => 0,
    'is_new' => TRUE,
  ));
  node_type_save($type);
  node_types_rebuild();
  menu_rebuild();

  _moby_setup_create_field(
    array('field_name' => 'field_mobile_description', 'type' => 'text_long'),
    array(
      'entity_type' => 'node',
      'bundle' => 'mobile',
      'label' => t('Description'),
      'settings' => array('text_processing' => 1),
      // Posts show their photos and videos, then the description, then the
      // location. Apps show them in the same order (see moby_post_order()).
      'display' => array('default' => array('label' => 'hidden', 'weight' => 2)),
    )
  );
  _moby_setup_create_field(
    array('field_name' => 'field_mobile_location', 'type' => 'text', 'settings' => array('max_length' => 255)),
    array(
      'entity_type' => 'node',
      'bundle' => 'mobile',
      'label' => t('Location'),
      'display' => array('default' => array('label' => 'hidden', 'weight' => 3)),
    )
  );
  _moby_setup_create_field(
    array('field_name' => 'field_mobile_media', 'type' => 'file', 'cardinality' => FIELD_CARDINALITY_UNLIMITED),
    array(
      'entity_type' => 'node',
      'bundle' => 'mobile',
      'label' => t('Photos and videos'),
      'settings' => array(
        'file_directory' => 'mobile',
        'file_extensions' => 'jpg jpeg png gif webp heic mp4 mov m4v webm',
        'description_field' => 1,
      ),
      'display' => array(
        'default' => array(
          'label' => 'hidden',
          'type' => 'moby_carousel',
          'settings' => array('image_style' => 'large'),
          'weight' => 1,
        ),
      ),
    )
  );

  // Date fields need the optional Date module. Without it, the node's authored
  // on date stores the date just as well.
  $date_field = 'created';
  if (module_exists('date')) {
    $date_field = 'field_mobile_date';
    _moby_setup_create_field(
      array('field_name' => $date_field, 'type' => 'datestamp'),
      array(
        'entity_type' => 'node',
        'bundle' => 'mobile',
        'label' => t('Date'),
        'display' => array('default' => array('weight' => 0)),
      )
    );
  }

  $formats = filter_formats();
  $format = config_get('moby.settings', 'mapping.description.format');
  return array(
    'description' => array(
      'field' => 'field_mobile_description',
      'format' => isset($formats[$format]) ? $format : filter_default_format(),
    ),
    'date' => array('field' => $date_field),
    'location' => array('name_field' => 'field_mobile_location'),
    'media' => array('field' => 'field_mobile_media'),
  );
}

/**
 * Creates a field and its instance, unless they already exist.
 *
 * @param array $field
 *   The field definition.
 * @param array $instance
 *   The instance definition, without its field name.
 */
function _moby_setup_create_field(array $field, array $instance) {
  $field_name = $field['field_name'];
  if (!field_info_field($field_name)) {
    field_create_field($field);
  }
  if (!field_info_instance($instance['entity_type'], $field_name, $instance['bundle'])) {
    field_create_instance($instance + array('field_name' => $field_name));
  }
}

/**
 * Builds step 2 of the setup wizard: Video file type and thumbnails.
 */
function moby_setup_wizard_form_video($form, &$form_state) {
  $form += moby_settings_form_video($form_state, 'wizard');

  // Default to the out-of-box file type, unless another has been chosen.
  $video = &$form['video']['video_file_type'];
  if (!isset($form_state['input']['mapping']) && !config_get('moby.settings', 'mapping.media.file_types.video') && isset($video['#options']['video'])) {
    $video['#default_value'] = 'video';
  }
  $file_type = (string) $video['#default_value'];
  unset($video);

  $thumbnail = &$form['video']['thumbnail_wrapper']['thumbnail_field'];
  $existing = $thumbnail['#options'];
  $can_create = !isset($existing['field_video_thumbnail']);
  $modes = array();
  if ($can_create) {
    $modes['new'] = t('Create a new "Video thumbnail" field (recommended)');
  }
  if ($existing) {
    $modes['existing'] = t('Use an existing image field on this file type');
  }
  $modes['none'] = t('Don\'t store video thumbnails');
  $default = 'none';
  if ($thumbnail['#default_value'] || !$can_create) {
    $default = 'existing';
    if (!$thumbnail['#default_value'] && !$can_create) {
      $thumbnail['#default_value'] = 'field_video_thumbnail';
    }
  }
  elseif ($can_create) {
    $default = 'new';
  }
  // The available modes change along with the file type.
  if (isset($form_state['input']['thumbnail_mode']) && !isset($modes[$form_state['input']['thumbnail_mode']])) {
    unset($form_state['input']['thumbnail_mode']);
  }

  $thumbnail['#empty_option'] = t('- Select a field -');
  $thumbnail['#states'] = array(
    'visible' => array(':input[name="thumbnail_mode"]' => array('value' => 'existing')),
  );
  unset($thumbnail['#description']);
  $form['video']['thumbnail_wrapper']['thumbnail_mode'] = array(
    '#type' => 'radios',
    '#title' => t('Video thumbnails'),
    '#description' => t('The app sends a thumbnail with each video, as servers usually can\'t make them. Which field should store it?'),
    '#options' => $modes,
    '#default_value' => $default,
    '#weight' => -1,
  );
  unset($thumbnail);

  $form['#validate'][] = 'moby_setup_wizard_form_video_validate';
  $form['#validate'][] = 'moby_settings_form_video_validate';
  $form['#submit'][] = 'moby_setup_wizard_form_video_submit';
  $form['#submit'][] = 'moby_settings_form_video_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Form validation handler for step 2 of the setup wizard.
 */
function moby_setup_wizard_form_video_validate($form, &$form_state) {
  $mode = $form_state['values']['thumbnail_mode'];
  if (empty($form_state['values']['mapping']['media']['file_types']['video'])) {
    form_set_error('mapping][media][file_types][video', t('Select a video file type.'));
  }
  if ($mode === 'existing' && empty($form_state['values']['mapping']['media']['thumbnail_field'])) {
    form_set_error('mapping][media][thumbnail_field', t('Select a field to store video thumbnails.'));
  }
  elseif ($mode !== 'existing') {
    // The selection is hidden, and may be for another file type.
    form_set_value($form['video']['thumbnail_wrapper']['thumbnail_field'], '', $form_state);
    if ($mode === 'new') {
      _moby_setup_validate_new_field('field_video_thumbnail', 'image', 'thumbnail_mode');
    }
  }
}

/**
 * Form submission handler for step 2 of the setup wizard.
 *
 * Creates the thumbnail field, if chosen, then sets the form's values so that
 * moby_settings_form_video_submit() saves it like any other.
 */
function moby_setup_wizard_form_video_submit($form, &$form_state) {
  if ($form_state['values']['thumbnail_mode'] === 'new') {
    _moby_setup_create_field(
      array('field_name' => 'field_video_thumbnail', 'type' => 'image', 'settings' => array('uri_scheme' => 'public')),
      array(
        'entity_type' => 'file',
        'bundle' => $form_state['values']['mapping']['media']['file_types']['video'],
        'label' => t('Video thumbnail'),
        'description' => t('An image representing the video, shown before it plays.'),
        'settings' => array(
          'file_directory' => 'video-thumbnails',
          'file_extensions' => 'jpg jpeg png webp',
          'max_dimensions' => '1920x1920',
        ),
        'display' => array(
          'default' => array('label' => 'hidden', 'type' => 'image'),
        ),
      )
    );
    $form_state['values']['mapping']['media']['thumbnail_field'] = 'field_video_thumbnail';
  }
}

/**
 * Builds step 3 of the setup wizard: The location's name and coordinates.
 */
function moby_setup_wizard_form_location($form, &$form_state) {
  $form['intro'] = array(
    '#markup' => '<p>' . t('The app can save the name of a place with each post. To also save its latitude and longitude, for example to show posts on a map, the site needs the <a href="@url">Geofield</a> module.', array('@url' => 'https://backdropcms.org/project/geofield')) . '</p>',
  );
  $form += moby_settings_form_location((string) config_get('moby.settings', 'node_type'), 'wizard');

  if (!module_exists('geofield')) {
    $form['geofield'] = array(
      '#markup' => '<p>' . t('Would you like to use location coordinates? If so, <a href="@modules">install and enable Geofield</a>, then check for it here. Otherwise, continue without them.', array('@modules' => url('admin/modules'))) . '</p>',
    );
    // Checking doesn't depend on any input, and saves nothing.
    $form['actions']['check'] = array(
      '#type' => 'submit',
      '#value' => t('Check for Geofield'),
      '#limit_validation_errors' => array(),
      '#submit' => array('moby_setup_wizard_form_location_check_submit'),
      '#weight' => 1,
    );
  }

  $form['#validate'][] = 'moby_setup_wizard_form_location_validate';
  $form['#submit'][] = 'moby_setup_wizard_form_location_submit';
  $form['#submit'][] = 'moby_settings_form_location_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Form submission handler: Reports whether the Geofield module is available.
 *
 * The step is then shown again, with the coordinates field if it is.
 */
function moby_setup_wizard_form_location_check_submit($form, &$form_state) {
  if (module_exists('geofield')) {
    backdrop_set_message(t('The Geofield module is enabled. You can now choose where to save coordinates.'));
  }
  elseif (backdrop_get_filename('module', 'geofield')) {
    backdrop_set_message(t('The Geofield module is installed but not enabled. <a href="@url">Enable it on the modules page</a>, then check again.', array('@url' => url('admin/modules'))), 'warning');
  }
  else {
    backdrop_set_message(t('The Geofield module was not found. Install and enable it, then check again.'), 'warning');
  }
  $form_state['redirect'] = moby_setup_wizard_path('location');
}

/**
 * Form validation handler for step 3 of the setup wizard.
 */
function moby_setup_wizard_form_location_validate($form, &$form_state) {
  if (isset($form_state['values']['mapping']['location']['coordinates_field']) && $form_state['values']['mapping']['location']['coordinates_field'] === MOBY_CREATE_FIELD) {
    _moby_setup_validate_new_field('field_mobile_coordinates', 'geofield', 'mapping][location][coordinates_field');
  }
}

/**
 * Form submission handler for step 3 of the setup wizard.
 *
 * Creates the coordinates field, if chosen, then sets the form's values so that
 * moby_settings_form_location_submit() saves it like any other.
 */
function moby_setup_wizard_form_location_submit($form, &$form_state) {
  if (isset($form_state['values']['mapping']['location']['coordinates_field']) && $form_state['values']['mapping']['location']['coordinates_field'] === MOBY_CREATE_FIELD) {
    _moby_setup_create_field(
      array('field_name' => 'field_mobile_coordinates', 'type' => 'geofield'),
      array(
        'entity_type' => 'node',
        'bundle' => config_get('moby.settings', 'node_type'),
        'label' => t('Coordinates'),
        'display' => array(
          'default' => array(
            'label' => 'hidden',
            'type' => 'geofield_latlon',
            'settings' => array('data' => 'full', 'format' => 'decimal_degrees', 'labels' => 0),
            'weight' => 4,
          ),
        ),
      )
    );
    $form_state['values']['mapping']['location']['coordinates_field'] = 'field_mobile_coordinates';
  }
}

/**
 * Builds step 4 of the setup wizard: Tags.
 */
function moby_setup_wizard_form_tags($form, &$form_state) {
  $form['intro'] = array(
    '#markup' => '<p>' . t('Posts from the app can have tags. Tags can be added in the app, and #hashtags in descriptions are added as tags too, without the "#". Which vocabulary should they be saved in?') . '</p>',
  );
  $form += moby_settings_form_tags('wizard');
  $form['#submit'][] = 'moby_settings_form_tags_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Builds step 5 of the setup wizard: The maximum upload size.
 */
function moby_setup_wizard_form_uploads($form, &$form_state) {
  $php_limit = file_upload_max_size();
  $limit = moby_max_upload_size();
  $recommended = format_size(MOBY_RECOMMENDED_UPLOAD_SIZE);
  if ($limit >= MOBY_RECOMMENDED_UPLOAD_SIZE) {
    $class = 'status';
    $message = t('The maximum upload size is %limit, which is enough for video content.', array('%limit' => format_size($limit)));
  }
  else {
    $class = 'warning';
    $message = t('The maximum upload size is %limit. We recommend at least %recommended for video content. The app compresses larger videos to fit, and rejects those it can\'t.', array('%limit' => format_size($limit), '%recommended' => $recommended));
    if ($php_limit < MOBY_RECOMMENDED_UPLOAD_SIZE) {
      $message .= ' ' . t('To increase it, set PHP\'s <code>upload_max_filesize</code> and <code>post_max_size</code> (in php.ini, or ask your hosting provider). They are currently %upload and %post.', array('%upload' => ini_get('upload_max_filesize'), '%post' => ini_get('post_max_size')));
    }
    if ($limit < $php_limit) {
      $message .= ' ' . t('The media field\'s "Maximum upload size" setting is also limiting uploads.');
    }
  }
  $form['report'] = array(
    '#markup' => '<div class="messages ' . $class . '">' . $message . '</div>',
  );
  $form += moby_settings_form_uploads('wizard');
  $form['#submit'][] = 'moby_settings_form_uploads_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Builds step 6 of the setup wizard: Permissions.
 */
function moby_setup_wizard_form_permissions($form, &$form_state) {
  $form['intro'] = array(
    '#markup' => '<p>' . t('Choose which roles can post from the app. To post, a role needs the <em>Post via Moby</em> permission, along with permission to create, edit, and delete its own content. To read back drafts it posts, it also needs to view its own unpublished content.') . '</p>',
  );
  if (user_access('administer permissions')) {
    $form['permissions'] = moby_setup_wizard_permissions_table((string) config_get('moby.settings', 'node_type'));
  }
  else {
    $form['no_access'] = array(
      '#markup' => '<div class="messages warning">' . t('You don\'t have permission to change these. A user who can administer permissions will need to grant them.') . '</div>',
    );
  }
  $form['#submit'][] = 'moby_setup_wizard_form_permissions_submit';
  $form['actions']['submit'] = array('#type' => 'submit', '#value' => t('Continue'));
  return $form;
}

/**
 * Returns a table of the permissions relevant to posting from the app.
 *
 * This extends the table on the content type form, which it looks like, with
 * the Moby permissions and others that posts depend on.
 *
 * @param string $node_type
 *   The content type machine name.
 *
 * @return array
 *   Form elements for theme_user_admin_permissions().
 *
 * @see node_type_form_permissions()
 */
function moby_setup_wizard_permissions_table($node_type) {
  module_load_include('inc', 'node', 'node.types');
  $table = node_type_form_permissions($node_type);

  $moby_permissions = moby_permission();
  $node_permissions = node_permission();
  $added = array(
    'post via moby' => $moby_permissions['post via moby'],
    'administer moby' => $moby_permissions['administer moby'],
    'access content' => $node_permissions['access content'],
    'view own unpublished content' => $node_permissions['view own unpublished content'],
  );
  $rows = array();
  foreach ($added as $permission => $info) {
    $info += array('description' => '', 'restrict access' => FALSE, 'warning' => '');
    $rows[$permission] = array(
      '#type' => 'item',
      '#markup' => $info['title'],
      '#description' => theme('user_permission_description', array('permission_item' => $info)),
    );
    foreach ($table['roles']['#value'] as $role_name => $role) {
      $table['checkboxes'][$role_name]['#options'][$permission] = '';
      if (in_array($permission, $role->permissions)) {
        $table['checkboxes'][$role_name]['#default_value'][] = $permission;
      }
    }
  }

  // Numeric keys are headings in the table.
  $type = node_type_get_type($node_type);
  $table['permission'] = array(
    array('#markup' => t('Moby'), '#id' => 'moby-permissions'),
    'post via moby' => $rows['post via moby'],
    'administer moby' => $rows['administer moby'],
    array('#markup' => check_plain($type->name), '#id' => 'moby-content-permissions'),
  ) + $table['permission'] + array(
    'access content' => $rows['access content'],
    'view own unpublished content' => $rows['view own unpublished content'],
  );
  return $table;
}

/**
 * Form submission handler for step 6 of the setup wizard.
 *
 * All steps have been completed, so the wizard is no longer suggested. Only the
 * permissions in the table change. A role keeps any others it has.
 */
function moby_setup_wizard_form_permissions_submit($form, &$form_state) {
  if (isset($form_state['values']['roles'])) {
    foreach ($form_state['values']['roles'] as $role_name => $role) {
      user_role_change_permissions($role_name, $form_state['values'][$role_name]);
    }
    backdrop_set_message(t('The permissions have been saved.'));
  }
  config_set('moby.settings', 'setup_complete', TRUE);
}

/**
 * Builds step 7 of the setup wizard: Done.
 */
function moby_setup_wizard_form_done($form, &$form_state) {
  module_load_include('inc', 'moby', 'moby.api');
  $issues = moby_setup_issues();
  $errors = array_filter($issues, function ($issue) {
    return $issue['severity'] === 'error';
  });

  $intro = $errors
    ? t('Moby needs some attention before it can be used to post.')
    : t('Moby is now ready to post!');
  $form['intro'] = array('#markup' => '<p><strong>' . $intro . '</strong></p>');
  if ($issues) {
    $form['issues'] = array(
      '#theme' => 'item_list',
      '#items' => array_map(function ($issue) {
        return check_plain($issue['message']);
      }, $issues),
    );
  }

  // @todo Link to the app listings once they have been created.
  $form['app'] = array(
    '#markup' => '<p>' . t('Download the Moby app from Google Play or the Apple App Store, then log in with the address of this site.') . '</p>',
  );
  // A role needs both permissions to post. See moby_user_can_create().
  $create_permission = 'create ' . config_get('moby.settings', 'node_type') . ' content';
  if (!array_intersect_key(user_roles(TRUE, 'post via moby'), user_roles(TRUE, $create_permission))) {
    $form['permissions'] = array(
      '#markup' => '<div class="messages warning">' . t('No role can post from the app yet, so only the site\'s first user can. A role needs both the <em>Post via Moby</em> permission and permission to create content. <a href="@url">Go back to choose permissions</a>.', array('@url' => url(moby_setup_wizard_path('permissions')))) . '</div>',
    );
  }
  $form['settings'] = array(
    '#markup' => '<p>' . t('Settings can be reviewed on the <a href="@url">Moby settings page</a>.', array('@url' => url('admin/config/services/moby'))) . '</p>',
  );
  return $form;
}

/**
 * Element validate callback for maximum upload size settings.
 *
 * Ensures the value can be parsed and does not exceed PHP's upload limit.
 */
function _moby_settings_form_validate_max_size($element, &$form_state) {
  $value = trim($element['#value']);
  if ($value === '') {
    return;
  }
  $size = parse_size($value);
  if (!preg_match('/^\d+(\.\d+)?\s*[bkmgtpezy]?b?$/i', $value) || $size <= 0) {
    form_error($element, t('The "!name" option must contain a valid value. You may either leave the text field empty or enter a string like "512" (bytes), "80 KB" (kilobytes) or "50 MB" (megabytes).', array('!name' => $element['#title'])));
  }
  elseif ($size > file_upload_max_size()) {
    form_error($element, t('The "!name" option may not exceed PHP\'s maximum upload size of %limit.', array('!name' => $element['#title'], '%limit' => format_size(file_upload_max_size()))));
  }
}

/**
 * Returns options for image fields attached to a file type.
 *
 * @param string|false $file_type
 *   The file type's machine name.
 *
 * @return array
 *   Field labels keyed by field name.
 */
function _moby_file_image_field_options($file_type) {
  $options = array();
  if (!$file_type) {
    return $options;
  }
  foreach (field_info_instances('file', $file_type) as $field_name => $instance) {
    $field = field_info_field($field_name);
    if ($field['type'] === 'image') {
      $options[$field_name] = t('@label (@name)', array('@label' => $instance['label'], '@name' => $field_name));
    }
  }
  return $options;
}

/**
 * Returns field options on a node type, limited to the given field types.
 *
 * @param string $node_type
 *   The node type machine name.
 * @param array $field_types
 *   The field types to include.
 * @param bool $multiple_only
 *   Whether to include only fields that accept more than one value.
 *
 * @return array
 *   Field labels keyed by field name.
 */
function _moby_field_options($node_type, array $field_types, $multiple_only = FALSE) {
  $options = array();
  foreach (field_info_instances('node', $node_type) as $field_name => $instance) {
    $field = field_info_field($field_name);
    if (!in_array($field['type'], $field_types)) {
      continue;
    }
    if ($multiple_only && $field['cardinality'] == 1) {
      continue;
    }
    $options[$field_name] = t('@label (@name)', array('@label' => $instance['label'], '@name' => $field_name));
  }
  return $options;
}
