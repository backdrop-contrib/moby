<?php
/**
 * @file
 * Username and password authentication mechanism.
 */

/**
 * Authenticates with a username (or email address) and password.
 *
 * Credentials are checked with the user login form's own validation handlers,
 * so the API follows the site's login method setting (username, email, or
 * either) and shares the login form's blocked account checks, flood control,
 * and log messages.
 */
class MobyAuthPassword extends MobyAuthMechanism {

  /**
   * {@inheritdoc}
   */
  public function getCredentialFields() {
    switch (config_get('system.core', 'user_login_method')) {
      case USER_LOGIN_EMAIL_ONLY:
        $label = t('Email address');
        $type = 'email';
        break;
      case USER_LOGIN_USERNAME_OR_EMAIL:
        $label = t('Username or email address');
        $type = 'text';
        break;
      default:
        $label = t('Username');
        $type = 'text';
    }
    return array(
      array(
        'name' => 'username',
        'label' => $label,
        'type' => $type,
        'required' => TRUE,
      ),
      array(
        'name' => 'password',
        'label' => t('Password'),
        'type' => 'password',
        'required' => TRUE,
      ),
    );
  }

  /**
   * {@inheritdoc}
   */
  public function authenticate(array $credentials) {
    $values = array();
    foreach (array('username' => 'name', 'password' => 'pass') as $property => $key) {
      if (!isset($credentials[$property]) || !is_string($credentials[$property]) || !strlen(trim($credentials[$property]))) {
        throw new MobyAuthException(422, 'validation_failed', t('The @property property is required.', array('@property' => $property)), $property);
      }
      $values[$key] = $credentials[$property];
    }

    // Run the same validation handlers as the user login form, in order.
    $form = array();
    $form_state = array('values' => $values);
    user_login_name_validate($form, $form_state);
    user_login_authenticate_validate($form, $form_state);
    user_login_final_validate($form, $form_state);

    // The handlers report problems as form errors. Clear them, along with the
    // messages they set, so they are not shown on a later page.
    $errors = form_get_errors();
    form_clear_error();
    backdrop_get_messages('error');

    if (!empty($form_state['flood_control_triggered'])) {
      throw new MobyAuthException(429, 'too_many_requests', $this->formatError($errors), 'username');
    }
    if ($errors || empty($form_state['uid'])) {
      $field = ($errors && key($errors) === 'pass') ? 'password' : 'username';
      throw new MobyAuthException(401, 'invalid_credentials', $this->formatError($errors), $field);
    }

    return user_load($form_state['uid']);
  }

  /**
   * {@inheritdoc}
   */
  public function getDiscoveryInfo() {
    $info = parent::getDiscoveryInfo();
    $info['password_reset_url'] = url('user/password', array('absolute' => TRUE));
    return $info;
  }

  /**
   * Converts the first login form error into a plain text API message.
   *
   * @param array|null $errors
   *   Form errors as returned by form_get_errors().
   */
  protected function formatError($errors) {
    if (empty($errors)) {
      return t('Sorry, unrecognized username or password.');
    }
    return trim(decode_entities(strip_tags(reset($errors))));
  }

}
