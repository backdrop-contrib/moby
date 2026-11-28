<?php
/**
 * @file
 * State of logins with more than one step.
 */

/**
 * A login in progress, between the steps of a multi-step mechanism.
 *
 * Nothing is stored for a login. Its state travels in its ID, which the app
 * sends with each step: a token holding the mechanism, the step, when it
 * expires, and the mechanism's data, signed with the site's private key so the
 * app can't change it. The data isn't encrypted, so the app can read it:
 * mechanisms keep secrets, such as an emailed code, as hashes made with
 * hashCode(), which can't be reversed or brute forced without the key.
 *
 * Each step has its own random nonce. Flood control counts wrong answers to
 * the step by its nonce, and records when the step is finished, so its ID
 * can't be used again.
 */
class MobyAuthRequest {

  /**
   * The flood event of a wrong answer to a step.
   */
  const FLOOD_ATTEMPT = 'moby_auth_step_attempt';

  /**
   * The flood event of a finished step, whose ID no longer works.
   */
  const FLOOD_USED = 'moby_auth_step_used';

  /**
   * The login's secret ID: the signed token the app sends back.
   *
   * @var string
   */
  public $id;

  /**
   * The machine name of the mechanism the login uses.
   *
   * @var string
   */
  public $mechanism;

  /**
   * The mechanism's name for the step the login is on.
   *
   * @var string
   */
  public $step = '';

  /**
   * The random nonce of the current step.
   *
   * @var string
   */
  public $nonce;

  /**
   * The mechanism's own data, such as the hash of a code that was sent.
   *
   * @var array
   */
  public $data = array();

  /**
   * The Unix timestamp when the login expires.
   *
   * @var int
   */
  public $expires = 0;

  /**
   * Starts a login.
   *
   * @param string $mechanism
   *   The mechanism's machine name.
   * @param string $step
   *   The mechanism's name for the step the login is on.
   * @param array $data
   *   The mechanism's own data. The app can read it, so it must not hold
   *   secrets, other than hashes from hashCode().
   * @param int $lifetime
   *   How many seconds the whole login may take.
   *
   * @return MobyAuthRequest
   *   The login, with its ID.
   */
  public static function create($mechanism, $step, array $data, $lifetime) {
    $request = new MobyAuthRequest();
    $request->mechanism = $mechanism;
    $request->expires = REQUEST_TIME + $lifetime;
    $request->setStep($step, $data);
    return $request;
  }

  /**
   * Loads an unexpired, unfinished login by its ID.
   *
   * @param string $mechanism
   *   The mechanism the login must use.
   * @param string $id
   *   The ID the app sent.
   *
   * @return MobyAuthRequest|false
   *   The login, or FALSE if the ID is invalid, has expired, or its step was
   *   already finished.
   */
  public static function load($mechanism, $id) {
    if (!is_string($id) || substr_count($id, '.') !== 1) {
      return FALSE;
    }
    list($payload, $signature) = explode('.', $id);
    if (!hash_equals(self::sign($payload), $signature)) {
      return FALSE;
    }
    $values = json_decode(base64_decode(strtr($payload, '-_', '+/')), TRUE);
    if (!is_array($values) || !isset($values['m'], $values['s'], $values['n'], $values['e'], $values['d']) || $values['m'] !== $mechanism || $values['e'] < REQUEST_TIME) {
      return FALSE;
    }
    $request = new MobyAuthRequest();
    $request->id = $id;
    $request->mechanism = $values['m'];
    $request->step = $values['s'];
    $request->nonce = $values['n'];
    $request->expires = (int) $values['e'];
    $request->data = (array) $values['d'];
    if ($request->isFinished()) {
      return FALSE;
    }
    return $request;
  }

  /**
   * Moves the login on to another step, with a new ID.
   *
   * The current step's ID stops working, and the new step has no wrong
   * answers yet.
   *
   * @param string $step
   *   The mechanism's name for the step.
   * @param array $data
   *   (optional) Data to add to the mechanism's own data.
   */
  public function setStep($step, array $data = array()) {
    if (isset($this->nonce)) {
      $this->finish();
    }
    $this->step = $step;
    $this->data = $data + $this->data;
    $this->nonce = backdrop_random_key(16);
    $payload = rtrim(strtr(base64_encode(json_encode(array(
      'm' => $this->mechanism,
      's' => $this->step,
      'n' => $this->nonce,
      'e' => $this->expires,
      'd' => $this->data,
    ))), '+/', '-_'), '=');
    $this->id = $payload . '.' . self::sign($payload);
  }

  /**
   * Counts a wrong answer to the current step.
   *
   * @return int
   *   How many wrong answers the step has had, including this one.
   */
  public function registerFailure() {
    $window = $this->window();
    flood_register_event(self::FLOOD_ATTEMPT, $window, $this->nonce);
    // Flood control only says whether a limit is reached, so find the count
    // by trying each limit in turn. Steps allow only a few wrong answers.
    $count = 1;
    while (!flood_is_allowed(self::FLOOD_ATTEMPT, $count, $window, $this->nonce)) {
      $count++;
    }
    return $count - 1;
  }

  /**
   * Ends the login's current step, so its ID can't be used again.
   *
   * @return bool
   *   TRUE if this request ended it. FALSE if it had already ended.
   */
  public function finish() {
    if ($this->isFinished()) {
      return FALSE;
    }
    flood_register_event(self::FLOOD_USED, $this->window(), $this->nonce);
    return TRUE;
  }

  /**
   * Hashes a secret of a login, such as an emailed code, for its data.
   *
   * The hash uses the site's private key, so the app can't work out the
   * secret from it, even for a 6-digit code.
   *
   * @param string $mechanism
   *   The mechanism's machine name.
   * @param string $secret
   *   The secret.
   *
   * @return string
   *   The hash.
   */
  public static function hashCode($mechanism, $secret) {
    return backdrop_hmac_base64('moby_auth_code:' . $mechanism . ':' . $secret, self::key());
  }

  /**
   * Whether the current step has already been finished.
   */
  protected function isFinished() {
    return !flood_is_allowed(self::FLOOD_USED, 1, $this->window(), $this->nonce);
  }

  /**
   * Returns how long flood events for this login need to be kept.
   */
  protected function window() {
    return max(1, $this->expires - REQUEST_TIME);
  }

  /**
   * Signs a login's payload.
   */
  protected static function sign($payload) {
    return backdrop_hmac_base64('moby_auth_request:' . $payload, self::key());
  }

  /**
   * Returns the key logins are signed with.
   */
  protected static function key() {
    return backdrop_get_private_key() . backdrop_get_hash_salt();
  }

}
