<?php
/**
 * @file
 * Test multi-step authentication mechanism.
 */

/**
 * A three-step login, for testing the apps: password, code, and questions.
 *
 * The first step is the username and password mechanism's. The later steps
 * are mocked: their descriptions give the code and the answers.
 */
class MobyAuthTest extends MobyAuthPassword {

  /**
   * The security questions, keyed by field name, with their answers.
   *
   * @return array
   *   Each value is an array with the "question" and the "answer".
   */
  protected function questions() {
    return array(
      'pet' => array('question' => t('What was the name of your first pet?'), 'answer' => 'Rex'),
      'color' => array('question' => t('What is your favorite color?'), 'answer' => 'Blue'),
      'city' => array('question' => t('In what city were you born?'), 'answer' => 'Paris'),
    );
  }

  /**
   * {@inheritdoc}
   */
  public function getSubmitLabel() {
    return t('Continue');
  }

  /**
   * {@inheritdoc}
   */
  public function getDescription() {
    return t('Step 1 of 3. This is a test login: after your password, you\'ll enter a code and answer three security questions.');
  }

  /**
   * {@inheritdoc}
   */
  public function authenticate(array $credentials) {
    if (!isset($credentials['request_id'])) {
      return $this->start($credentials);
    }
    $request = $this->loadRequest($credentials);
    switch ($request->step) {
      case 'code':
        return $this->verifyCode($request, $credentials);

      case 'questions':
        return $this->verifyAnswers($request, $credentials);
    }
    $request->finish();
    throw new MobyAuthException(410, 'login_expired', t('This login has expired. Please log in again.'), NULL, TRUE);
  }

  /**
   * Step 1: checks the username and password, then asks for the code.
   */
  protected function start(array $credentials) {
    $account = parent::authenticate($credentials);
    if (!flood_is_allowed('moby_auth_test_lockout', 1, MOBY_AUTH_TEST_LOCKOUT, $account->uid)) {
      throw new MobyAuthException(429, 'too_many_attempts', $this->lockoutMessage(), NULL, TRUE);
    }
    $code = sprintf('%06d', random_int(0, 999999));
    $request = MobyAuthRequest::create($this->getName(), 'code', array(
      'uid' => (int) $account->uid,
      'code_hash' => MobyAuthRequest::hashCode($this->getName(), $code),
    ), 900);
    return $this->codeStep($request, $account, $code);
  }

  /**
   * Step 2: checks the code, then asks the security questions.
   */
  protected function verifyCode(MobyAuthRequest $request, array $credentials) {
    $code = isset($credentials['code']) && is_string($credentials['code']) ? preg_replace('/\s+/', '', $credentials['code']) : '';
    if (!preg_match('/^\d{6}$/', $code)) {
      throw new MobyAuthException(422, 'validation_failed', t('Enter the 6-digit code from the email.'), 'code');
    }
    if (hash_equals($request->data['code_hash'], MobyAuthRequest::hashCode($this->getName(), $code))) {
      $request->setStep('questions');
      return $this->questionsStep($request);
    }
    $left = MOBY_AUTH_TEST_MAX_ATTEMPTS - $request->registerFailure();
    if ($left <= 0) {
      $request->finish();
      throw new MobyAuthException(401, 'too_many_attempts', t('Invalid email code. Please log in again.'), 'code', TRUE);
    }
    throw new MobyAuthException(401, 'invalid_code', format_plural($left, 'That code isn\'t right. You can try 1 more time.', 'That code isn\'t right. You can try @count more times.'), 'code');
  }

  /**
   * Step 3: checks the answers, then returns the account.
   */
  protected function verifyAnswers(MobyAuthRequest $request, array $credentials) {
    $wrong = NULL;
    foreach ($this->questions() as $name => $question) {
      $answer = isset($credentials[$name]) && is_string($credentials[$name]) ? trim($credentials[$name]) : '';
      if ($answer === '') {
        throw new MobyAuthException(422, 'validation_failed', t('Answer every question.'), $name);
      }
      if (!isset($wrong) && backdrop_strtolower($answer) !== backdrop_strtolower($question['answer'])) {
        $wrong = $name;
      }
    }
    if (!isset($wrong)) {
      return $this->finishLogin($request, user_load($request->data['uid']));
    }

    $left = MOBY_AUTH_TEST_MAX_ATTEMPTS - $request->registerFailure();
    if ($left <= 0) {
      $request->finish();
      flood_register_event('moby_auth_test_lockout', MOBY_AUTH_TEST_LOCKOUT, $request->data['uid']);
      throw new MobyAuthException(429, 'too_many_attempts', $this->lockoutMessage(), $wrong, TRUE);
    }
    throw new MobyAuthException(401, 'invalid_credentials', format_plural($left, 'At least one answer isn\'t right. You can try 1 more time.', 'At least one answer isn\'t right. You can try @count more times.'), $wrong);
  }

  /**
   * Builds the step that asks for the code.
   */
  protected function codeStep(MobyAuthRequest $request, $account, $code) {
    $fields = array(
      array(
        'name' => 'code',
        'label' => t('6-digit code'),
        'type' => 'number',
        'required' => TRUE,
      ),
    );
    $description = t('Step 2 of 3. We\'ve emailed a 6-digit code to @mail. This is a test, so no email was sent: the code is @code.', array(
      '@mail' => $this->maskMail($account->mail),
      '@code' => $code,
    ));
    return new MobyAuthStep($request, $fields, t('Check your email'), $description, t('Continue'));
  }

  /**
   * Builds the step that asks the security questions.
   */
  protected function questionsStep(MobyAuthRequest $request) {
    $fields = array();
    $answers = array();
    foreach ($this->questions() as $name => $question) {
      $fields[] = array(
        'name' => $name,
        'label' => $question['question'],
        'type' => 'text',
        'required' => TRUE,
      );
      $answers[] = $question['answer'];
    }
    $description = t('Step 3 of 3. Answer your security questions. This is a test, so the answers are: @answers.', array('@answers' => implode(', ', $answers)));
    return new MobyAuthStep($request, $fields, t('Security questions'), $description, t('Log in'));
  }

  /**
   * Returns the message shown when the account is locked out.
   */
  protected function lockoutMessage() {
    return format_plural(round(MOBY_AUTH_TEST_LOCKOUT / 60), 'Too many invalid login attempts, please wait 1 minute and try again.', 'Too many invalid login attempts, please wait @count minutes and try again.');
  }

  /**
   * Hides most of an email address, such as "n•••@example.com".
   */
  protected function maskMail($mail) {
    list($local, $domain) = array_pad(explode('@', $mail, 2), 2, '');
    return backdrop_substr($local, 0, 1) . '•••@' . $domain;
  }

}
