<?php
/**
 * @file
 * Base class and exception for Moby authentication mechanisms.
 */

/**
 * Base class for authentication mechanisms.
 *
 * An authentication mechanism verifies the credentials the app sends to
 * POST /auth/token and returns the matching user account. Moby
 * then issues an API token for that account, so token storage and validation
 * are the same regardless of the mechanism in use.
 *
 * Mechanisms are declared with hook_moby_auth_info(), and the active mechanism
 * is selected at admin/config/services/moby.
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
   * Returns the credentials the app must collect to log in.
   *
   * The app builds its login form from this list.
   *
   * @return array
   *   A list of fields, each an array containing:
   *   - name: The property name to send in the POST /auth/token request.
   *   - label: A human-readable label.
   *   - type: The kind of input: "text", "email", or "password".
   *   - required: Whether the field must be provided.
   */
  abstract public function getCredentialFields();

  /**
   * Verifies credentials and returns the matching user account.
   *
   * @param array $credentials
   *   The decoded body of the POST /auth/token request.
   *
   * @return User
   *   The authenticated, active user account.
   *
   * @throws MobyAuthException
   *   When the credentials are missing, invalid, or rate limited.
   */
  abstract public function authenticate(array $credentials);

  /**
   * Returns information about this mechanism for the discovery endpoint.
   *
   * @return array
   *   The "auth" property of the GET / response.
   */
  public function getDiscoveryInfo() {
    return array(
      'mechanism' => $this->getName(),
      'label' => $this->getLabel(),
      'fields' => $this->getCredentialFields(),
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
   */
  public function __construct($status, $error_code, $message, $field = NULL) {
    parent::__construct($message);
    $this->status = $status;
    $this->errorCode = $error_code;
    $this->field = $field;
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

}
