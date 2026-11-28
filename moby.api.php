<?php
/**
 * @file
 * Hooks provided by Moby.
 */

/**
 * @addtogroup hooks
 * @{
 */

/**
 * Declares authentication mechanisms for the Moby API.
 *
 * An authentication mechanism verifies the credentials sent to
 * POST /api/moby/auth/token. Once a mechanism returns a user account,
 * Moby issues an API token for it. Sites enable one or more mechanisms at
 * admin/config/services/moby, in the order the app offers them; the first is
 * the default. A newly declared mechanism is not enabled until an
 * administrator enables it, unless the module adds it to the
 * "auth_mechanisms" list in moby.settings itself.
 *
 * @return array
 *   An array of mechanisms keyed by machine name. Each value is an array
 *   containing:
 *   - label: The translated, human-readable name of the mechanism.
 *   - description: (optional) A translated description for administrators.
 *   - class: The name of a class that extends MobyAuthMechanism. Use
 *     hook_autoload_info() to make it available.
 *
 * @see MobyAuthMechanism
 * @see hook_moby_auth_info_alter()
 */
function hook_moby_auth_info() {
  $mechanisms['ldap'] = array(
    'label' => t('LDAP'),
    'description' => t('Log in with directory credentials.'),
    'class' => 'MyModuleMobyAuthLdap',
  );
  return $mechanisms;
}

/**
 * Alters authentication mechanisms declared by hook_moby_auth_info().
 *
 * @param array $mechanisms
 *   The mechanisms, keyed by machine name.
 */
function hook_moby_auth_info_alter(array &$mechanisms) {
  // Replace the class used by the username and password mechanism.
  $mechanisms['password']['class'] = 'MyModuleMobyAuthPassword';
}

/**
 * @} End of "addtogroup hooks".
 */
