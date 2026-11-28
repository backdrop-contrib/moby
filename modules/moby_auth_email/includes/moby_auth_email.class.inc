<?php
/**
 * @file
 * Email login authentication mechanism.
 */

/**
 * Authenticates with a code emailed to the account's address.
 *
 * Logging in takes two steps, each a POST /auth/token request:
 *
 * 1. With "email": the site emails a 6-digit code to the account with that
 *    address, and answers with a step that asks for it. It answers the same
 *    whether or not an account uses the address, so the API doesn't reveal
 *    who has an account.
 * 2. With "request_id" and "code": the site checks the code, and answers with
 *    the account's token. After too many wrong codes, the login restarts, so
 *    a new code must be sent.
 */
class MobyAuthEmail extends MobyAuthMechanism {

  /**
   * {@inheritdoc}
   */
  public function getSubmitLabel() {
    return t('Email me a code');
  }

  /**
   * {@inheritdoc}
   */
  public function getDescription() {
    return t('We\'ll email you a 6-digit code to log in with.');
  }

  /**
   * {@inheritdoc}
   */
  public function getCredentialFields() {
    return array(
      array(
        'name' => 'email',
        'label' => t('Email address'),
        'type' => 'email',
        'required' => TRUE,
        'placeholder' => t('name@example.com'),
      ),
    );
  }

  /**
   * {@inheritdoc}
   */
  public function authenticate(array $credentials) {
    if (isset($credentials['request_id'])) {
      return $this->verify($credentials);
    }
    return $this->start($credentials);
  }

  /**
   * Emails a code, and returns the step that asks for it.
   */
  protected function start(array $credentials) {
    $mail = isset($credentials['email']) && is_string($credentials['email']) ? trim($credentials['email']) : '';
    if ($mail === '') {
      throw new MobyAuthException(422, 'validation_failed', t('The email property is required.'), 'email');
    }
    if (!valid_email_address($mail)) {
      throw new MobyAuthException(422, 'validation_failed', t('Enter a valid email address.'), 'email');
    }

    // Limit how many emails can be sent, per IP address and per address
    // emailed. The window is an hour.
    $mail_key = backdrop_strtolower($mail);
    if (!flood_is_allowed('moby_auth_email_ip', MOBY_AUTH_EMAIL_IP_LIMIT) || !flood_is_allowed('moby_auth_email_mail', MOBY_AUTH_EMAIL_MAIL_LIMIT, 3600, $mail_key)) {
      throw new MobyAuthException(429, 'too_many_requests', t('Too many login codes have been requested. Please wait an hour and try again.'), 'email');
    }
    flood_register_event('moby_auth_email_ip');
    flood_register_event('moby_auth_email_mail', 3600, $mail_key);

    $device_name = (isset($credentials['device_name']) && is_string($credentials['device_name'])) ? trim($credentials['device_name']) : '';
    if ($device_name === '') {
      $device_name = t('Mobile device');
    }
    $code = sprintf('%06d', random_int(0, 999999));

    // The response is the same whether or not an active account uses the
    // address, and the login's ID holds only the address typed, so neither
    // reveals who has an account. Without an account, no code is sent.
    $account = user_load_by_mail($mail);
    $request = MobyAuthRequest::create($this->getName(), 'code', array(
      'code_hash' => MobyAuthRequest::hashCode($this->getName(), $code),
      'mail' => $mail,
    ), moby_auth_email_code_lifetime());

    if ($account && $account->status) {
      $params = array(
        'account' => $account,
        'device_name' => $device_name,
        'code' => $code,
      );
      backdrop_mail('moby_auth_email', 'login', $account->mail, user_preferred_language($account), $params);
      watchdog('moby', 'Login code emailed to %name for %device.', array('%name' => $account->name, '%device' => $device_name));
    }
    else {
      watchdog('moby', 'Login code requested for %mail, which no active account uses.', array('%mail' => $mail), WATCHDOG_NOTICE);
    }

    return $this->codeStep($request);
  }

  /**
   * Checks an emailed code, and returns the account if it matches.
   */
  protected function verify(array $credentials) {
    $request = $this->loadRequest($credentials);
    $code = isset($credentials['code']) && is_string($credentials['code']) ? preg_replace('/\s+/', '', $credentials['code']) : '';
    if (!preg_match('/^\d{6}$/', $code)) {
      throw new MobyAuthException(422, 'validation_failed', t('Enter the 6-digit code from the email.'), 'code');
    }

    if (hash_equals($request->data['code_hash'], MobyAuthRequest::hashCode($this->getName(), $code))) {
      return $this->finishLogin($request, user_load_by_mail($request->data['mail']));
    }

    $left = moby_auth_email_max_attempts() - $request->registerFailure();
    if ($left <= 0) {
      $request->finish();
      throw new MobyAuthException(401, 'too_many_attempts', t('Invalid email code. Please log in again.'), 'code', TRUE);
    }
    throw new MobyAuthException(401, 'invalid_code', format_plural($left, 'That code isn\'t right. You can try 1 more time.', 'That code isn\'t right. You can try @count more times.'), 'code');
  }

  /**
   * Builds the step that asks for the emailed code.
   */
  protected function codeStep(MobyAuthRequest $request) {
    $fields = array(
      array(
        'name' => 'code',
        'label' => t('6-digit code'),
        'type' => 'number',
        'required' => TRUE,
      ),
    );
    // Only the address the user typed is shown, so this never reveals an
    // account's address.
    $description = t('If an account on @site uses @mail, we\'ve emailed it a 6-digit code. Enter it here within @minutes minutes.', array(
      '@site' => config_get('system.core', 'site_name'),
      '@mail' => $request->data['mail'],
      '@minutes' => round(moby_auth_email_code_lifetime() / 60),
    ));
    return new MobyAuthStep($request, $fields, t('Check your email'), $description, t('Log in'));
  }

}
