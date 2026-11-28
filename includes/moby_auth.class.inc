<?php
/**
 * @file
 * Base class, login step, and exception for Moby authentication mechanisms.
 */

/**
 * Base class for authentication mechanisms.
 *
 * An authentication mechanism verifies the credentials the app sends to
 * POST /auth/token and returns the matching user account. Moby
 * then issues an API token for that account, so token storage and validation
 * are the same regardless of the mechanism in use.
 *
 * A mechanism that needs more than one form, such as one that emails a code
 * or asks for a second factor, returns a MobyAuthStep instead: the next form
 * for the app to show. The app sends that form to POST /auth/token with the
 * step's request ID, and the mechanism answers with another step or the
 * account. MobyAuthRequest carries the login's state between steps, in the
 * step's ID.
 *
 * Mechanisms are declared with hook_moby_auth_info(). Sites enable one or more
 * at admin/config/services/moby, in order, and the first is the default.
 */
abstract class MobyAuthMechanism {

  /**
   * The machine name of this mechanism.
   *
   * @var string
   */
  protected $name;

  /**
   * The mechanism's definition from hook_moby_auth_info().
   *
   * @var array
   */
  protected $info;

  /**
   * Constructs an authentication mechanism.
   *
   * @param string $name
   *   The machine name of the mechanism.
   * @param array $info
   *   The mechanism's definition from hook_moby_auth_info().
   */
  public function __construct($name, array $info) {
    $this->name = $name;
    $this->info = $info;
  }

  /**
   * Returns the machine name of this mechanism.
   */
  public function getName() {
    return $this->name;
  }

  /**
   * Returns the human-readable label of this mechanism.
   */
  public function getLabel() {
    return $this->info['label'];
  }

  /**
   * Returns the oldest API version whose apps can use this mechanism.
   *
   * Apps for older versions don't see the mechanism, and can't use it.
   * Override this when a mechanism needs an API change made after the oldest
   * version the module serves.
   *
   * @return int
   *   The API version.
   */
  public function getMinApiVersion() {
    return MOBY_API_MIN_VERSION;
  }

  /**
   * Returns the label of the app's button that submits the credentials.
   */
  public function getSubmitLabel() {
    return t('Log in');
  }

  /**
   * Returns a sentence the app shows above the credential fields.
   *
   * @return string|null
   *   Plain text, or NULL for none.
   */
  public function getDescription() {
    return NULL;
  }

  /**
   * Returns the credentials the app must collect to log in.
   *
   * The app builds its login form from this list.
   *
   * @return array
   *   A list of fields, each an array containing:
   *   - name: The property name to send in the POST /auth/token request.
   *   - label: A human-readable label.
   *   - type: The kind of input: "text", "email", "password", or "number".
   *     A number field takes digits only, which the app sends as a string, so
   *     leading zeros are kept.
   *   - required: Whether the field must be provided.
   *   - placeholder: (optional) Example text shown in the empty field, such
   *     as "name@example.com". Leave it out to have none.
   */
  abstract public function getCredentialFields();

  /**
   * Verifies credentials and returns the matching user account.
   *
   * @param array $credentials
   *   The decoded body of the POST /auth/token request.
   *
   * @return User|MobyAuthStep
   *   The authenticated, active user account. Or, for a mechanism with more
   *   than one form, the next step. The app sends the step's fields in a
   *   POST /auth/token request with the mechanism's name in "mechanism" and
   *   the step's ID in "request_id".
   *
   * @throws MobyAuthException
   *   When the credentials are missing, invalid, or rate limited. Throw one
   *   that restarts the login when the user must start again from the first
   *   form, such as after too many wrong codes.
   */
  abstract public function authenticate(array $credentials);

  /**
   * Returns information about this mechanism for the discovery endpoint.
   *
   * @return array
   *   An item of the "auth_mechanisms" property of the GET /meta response.
   */
  public function getDiscoveryInfo() {
    return array(
      'mechanism' => $this->getName(),
      'label' => $this->getLabel(),
      'description' => $this->getDescription(),
      'submit_label' => $this->getSubmitLabel(),
      'fields' => self::normalizeFields($this->getCredentialFields()),
    );
  }

  /**
   * Adds the optional properties of credential fields that are left out.
   *
   * @param array $fields
   *   Fields, as returned by getCredentialFields().
   *
   * @return array
   *   The fields, as the API lists them.
   */
  public static function normalizeFields(array $fields) {
    return array_map(function ($field) {
      return $field + array('required' => TRUE, 'placeholder' => NULL);
    }, $fields);
  }

  /**
   * Loads the login that a later step continues.
   *
   * @param array $credentials
   *   The decoded body of the POST /auth/token request, with "request_id".
   *
   * @return MobyAuthRequest
   *   The login.
   *
   * @throws MobyAuthException
   *   A restarting error, if the login doesn't exist or has expired.
   */
  protected function loadRequest(array $credentials) {
    $request = isset($credentials['request_id']) ? MobyAuthRequest::load($this->getName(), $credentials['request_id']) : FALSE;
    if (!$request) {
      throw new MobyAuthException(410, 'login_expired', t('This login has expired. Please log in again.'), NULL, TRUE);
    }
    return $request;
  }

  /**
   * Finishes a login, and returns its account if it is active.
   *
   * @param MobyAuthRequest $request
   *   The login.
   * @param User|false $account
   *   The account it is for.
   *
   * @return User
   *   The account.
   *
   * @throws MobyAuthException
   *   A restarting error, if the login was already finished, such as by
   *   another request sent at the same time, or the account is blocked or no
   *   longer exists.
   */
  protected function finishLogin(MobyAuthRequest $request, $account) {
    if (!$request->finish()) {
      throw new MobyAuthException(410, 'login_expired', t('This login has expired. Please log in again.'), NULL, TRUE);
    }
    if (!$account || !$account->status) {
      throw new MobyAuthException(401, 'invalid_credentials', t('This account is blocked or no longer exists.'), NULL, TRUE);
    }
    return $account;
  }

}

/**
 * The next form of a login with more than one step.
 *
 * Returned by MobyAuthMechanism::authenticate(). The API answers with it as a
 * 202 response, and the app shows its fields in place of the previous form.
 */
class MobyAuthStep {

  /**
   * The login this step continues.
   *
   * @var MobyAuthRequest
   */
  protected $request;

  /**
   * The fields, in the format of MobyAuthMechanism::getCredentialFields().
   *
   * @var array
   */
  protected $fields;

  /**
   * The step's heading, or NULL for the app's usual one.
   *
   * @var string|null
   */
  protected $title;

  /**
   * A plain text paragraph shown above the fields, or NULL.
   *
   * @var string|null
   */
  protected $description;

  /**
   * The label of the button that sends the fields.
   *
   * @var string
   */
  protected $submitLabel;

  /**
   * Constructs a login step.
   *
   * @param MobyAuthRequest $request
   *   The login this step continues. Its ID must be known, as it is when the
   *   login has just been created or loaded.
   * @param array $fields
   *   The fields to collect, as for MobyAuthMechanism::getCredentialFields().
   * @param string|null $title
   *   (optional) A heading, such as "Check your email".
   * @param string|null $description
   *   (optional) Plain text shown above the fields.
   * @param string|null $submit_label
   *   (optional) The label of the button. Defaults to "Continue".
   */
  public function __construct(MobyAuthRequest $request, array $fields, $title = NULL, $description = NULL, $submit_label = NULL) {
    $this->request = $request;
    $this->fields = $fields;
    $this->title = $title;
    $this->description = $description;
    $this->submitLabel = isset($submit_label) ? $submit_label : t('Continue');
  }

  /**
   * Returns the "step" property of the 202 response.
   */
  public function toArray() {
    return array(
      'request_id' => $this->request->id,
      'title' => $this->title,
      'description' => $this->description,
      'submit_label' => $this->submitLabel,
      'fields' => MobyAuthMechanism::normalizeFields($this->fields),
      'expires_in' => max(0, $this->request->expires - REQUEST_TIME),
    );
  }

}

/**
 * Exception thrown when authentication fails.
 *
 * Carries the HTTP status and error code of the resulting API error response.
 */
class MobyAuthException extends Exception {

  /**
   * The HTTP status code.
   *
   * @var int
   */
  protected $status;

  /**
   * The machine-readable error code.
   *
   * @var string
   */
  protected $errorCode;

  /**
   * The request property that caused the error, if any.
   *
   * @var string|null
   */
  protected $field;

  /**
   * Whether the app must start the login again from the first form.
   *
   * @var bool
   */
  protected $restart;

  /**
   * Constructs an authentication exception.
   *
   * @param int $status
   *   The HTTP status code, such as 401.
   * @param string $error_code
   *   The error code, such as "invalid_credentials".
   * @param string $message
   *   A human-readable error message.
   * @param string|null $field
   *   (optional) The request property that caused the error.
   * @param bool $restart
   *   (optional) TRUE if the login can't go on, and the app must start again
   *   from the first form. Otherwise the app keeps the current form, so the
   *   user can correct it.
   */
  public function __construct($status, $error_code, $message, $field = NULL, $restart = FALSE) {
    parent::__construct($message);
    $this->status = $status;
    $this->errorCode = $error_code;
    $this->field = $field;
    $this->restart = $restart;
  }

  /**
   * Returns the HTTP status code.
   */
  public function getStatus() {
    return $this->status;
  }

  /**
   * Returns the machine-readable error code.
   */
  public function getErrorCode() {
    return $this->errorCode;
  }

  /**
   * Returns the request property that caused the error, if any.
   */
  public function getField() {
    return $this->field;
  }

  /**
   * Whether the app must start the login again from the first form.
   */
  public function isRestart() {
    return $this->restart;
  }

}
