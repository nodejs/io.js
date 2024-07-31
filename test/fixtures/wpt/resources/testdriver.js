(function() {
    "use strict";
    var idCounter = 0;
    let testharness_context = null;

    function getInViewCenterPoint(rect) {
        var left = Math.max(0, rect.left);
        var right = Math.min(window.innerWidth, rect.right);
        var top = Math.max(0, rect.top);
        var bottom = Math.min(window.innerHeight, rect.bottom);

        var x = 0.5 * (left + right);
        var y = 0.5 * (top + bottom);

        return [x, y];
    }

    function getPointerInteractablePaintTree(element) {
        let elementDocument = element.ownerDocument;
        if (!elementDocument.contains(element)) {
            return [];
        }

        var rectangles = element.getClientRects();

        if (rectangles.length === 0) {
            return [];
        }

        var centerPoint = getInViewCenterPoint(rectangles[0]);

        if ("elementsFromPoint" in elementDocument) {
            return elementDocument.elementsFromPoint(centerPoint[0], centerPoint[1]);
        } else if ("msElementsFromPoint" in elementDocument) {
            var rv = elementDocument.msElementsFromPoint(centerPoint[0], centerPoint[1]);
            return Array.prototype.slice.call(rv ? rv : []);
        } else {
            throw new Error("document.elementsFromPoint unsupported");
        }
    }

    function inView(element) {
        var pointerInteractablePaintTree = getPointerInteractablePaintTree(element);
        return pointerInteractablePaintTree.indexOf(element) !== -1;
    }


    /**
     * @namespace {test_driver}
     */
    window.test_driver = {
        /**
         * Set the context in which testharness.js is loaded
         *
         * @param {WindowProxy} context - the window containing testharness.js
         **/
        set_test_context: function(context) {
          if (window.test_driver_internal.set_test_context) {
            window.test_driver_internal.set_test_context(context);
          }
          testharness_context = context;
        },

        /**
         * postMessage to the context containing testharness.js
         *
         * @param {Object} msg - the data to POST
         **/
        message_test: function(msg) {
            let target = testharness_context;
            if (testharness_context === null) {
                target = window;
            }
            target.postMessage(msg, "*");
        },

        /**
         * Trigger user interaction in order to grant additional privileges to
         * a provided function.
         *
         * See `Tracking user activation
         * <https://html.spec.whatwg.org/multipage/interaction.html#tracking-user-activation>`_.
         *
         * @example
         * var mediaElement = document.createElement('video');
         *
         * test_driver.bless('initiate media playback', function () {
         *   mediaElement.play();
         * });
         *
         * @param {String} intent - a description of the action which must be
         *                          triggered by user interaction
         * @param {Function} action - code requiring escalated privileges
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled following user interaction and
         *                    execution of the provided `action` function;
         *                    rejected if interaction fails or the provided
         *                    function throws an error
         */
        bless: function(intent, action, context=null) {
            let contextDocument = context ? context.document : document;
            var button = contextDocument.createElement("button");
            button.innerHTML = "This test requires user interaction.<br />" +
                "Please click here to allow " + intent + ".";
            button.id = "wpt-test-driver-bless-" + (idCounter += 1);
            const elem = contextDocument.body || contextDocument.documentElement;
            elem.appendChild(button);

            let wait_click = new Promise(resolve => button.addEventListener("click", resolve));

            return test_driver.click(button)
                .then(wait_click)
                .then(function() {
                    button.remove();

                    if (typeof action === "function") {
                        return action();
                    }
                    return null;
                });
        },

        /**
         * Triggers a user-initiated click
         *
         * If ``element`` isn't inside the
         * viewport, it will be scrolled into view before the click
         * occurs.
         *
         * If ``element`` is from a different browsing context, the
         * command will be run in that context.
         *
         * Matches the behaviour of the `Element Click
         * <https://w3c.github.io/webdriver/#element-click>`_
         * WebDriver command.
         *
         * **Note:** If the element to be clicked does not have a
         * unique ID, the document must not have any DOM mutations
         * made between the function being called and the promise
         * settling.
         *
         * @param {Element} element - element to be clicked
         * @returns {Promise} fulfilled after click occurs, or rejected in
         *                    the cases the WebDriver command errors
         */
        click: function(element) {
            if (!inView(element)) {
                element.scrollIntoView({behavior: "instant",
                                        block: "end",
                                        inline: "nearest"});
            }

            var pointerInteractablePaintTree = getPointerInteractablePaintTree(element);
            if (pointerInteractablePaintTree.length === 0 ||
                !element.contains(pointerInteractablePaintTree[0])) {
                return Promise.reject(new Error("element click intercepted error"));
            }

            var rect = element.getClientRects()[0];
            var centerPoint = getInViewCenterPoint(rect);
            return window.test_driver_internal.click(element,
                                                     {x: centerPoint[0],
                                                      y: centerPoint[1]});
        },

        /**
         * Deletes all cookies.
         *
         * Matches the behaviour of the `Delete All Cookies
         * <https://w3c.github.io/webdriver/#delete-all-cookies>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after cookies are deleted, or rejected in
         *                    the cases the WebDriver command errors
         */
        delete_all_cookies: function(context=null) {
            return window.test_driver_internal.delete_all_cookies(context);
        },

        /**
         * Get details for all cookies in the current context.
         * See https://w3c.github.io/webdriver/#get-all-cookies
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Returns an array of cookies objects as defined in the spec:
         *                    https://w3c.github.io/webdriver/#cookies
         */
        get_all_cookies: function(context=null) {
            return window.test_driver_internal.get_all_cookies(context);
        },

        /**
         * Get details for a cookie in the current context by name if it exists.
         * See https://w3c.github.io/webdriver/#get-named-cookie
         *
         * @param {String} name - The name of the cookie to get.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Returns the matching cookie as defined in the spec:
         *                    https://w3c.github.io/webdriver/#cookies
         *                    Rejected if no such cookie exists.
         */
         get_named_cookie: async function(name, context=null) {
            let cookie = await window.test_driver_internal.get_named_cookie(name, context);
            if (!cookie) {
                throw new Error("no such cookie");
            }
            return cookie;
        },

        /**
         * Get Computed Label for an element.
         *
         * This matches the behaviour of the
         * `Get Computed Label
         * <https://w3c.github.io/webdriver/#dfn-get-computed-label>`_
         * WebDriver command.
         *
         * @param {Element} element
         * @returns {Promise} fulfilled after the computed label is returned, or
         *                    rejected in the cases the WebDriver command errors
         */
        get_computed_label: async function(element) {
            let label = await window.test_driver_internal.get_computed_label(element);
            return label;
        },

        /**
         * Get Computed Role for an element.
         *
         * This matches the behaviour of the
         * `Get Computed Label
         * <https://w3c.github.io/webdriver/#dfn-get-computed-role>`_
         * WebDriver command.
         *
         * @param {Element} element
         * @returns {Promise} fulfilled after the computed role is returned, or
         *                    rejected in the cases the WebDriver command errors
         */
        get_computed_role: async function(element) {
            let role = await window.test_driver_internal.get_computed_role(element);
            return role;
        },

        /**
         * Send keys to an element.
         *
         * If ``element`` isn't inside the
         * viewport, it will be scrolled into view before the click
         * occurs.
         *
         * If ``element`` is from a different browsing context, the
         * command will be run in that context. The test must not depend
         * on the ``window.name`` property being unset on the target
         * window.
         *
         * To send special keys, send the respective key's codepoint,
         * as defined by `WebDriver
         * <https://w3c.github.io/webdriver/#keyboard-actions>`_.  For
         * example, the "tab" key is represented as "``\uE004``".
         *
         * **Note:** these special-key codepoints are not necessarily
         * what you would expect. For example, <kbd>Esc</kbd> is the
         * invalid Unicode character ``\uE00C``, not the ``\u001B`` Escape
         * character from ASCII.
         *
         * This matches the behaviour of the
         * `Send Keys
         * <https://w3c.github.io/webdriver/#element-send-keys>`_
         * WebDriver command.
         *
         * **Note:** If the element to be clicked does not have a
         * unique ID, the document must not have any DOM mutations
         * made between the function being called and the promise
         * settling.
         *
         * @param {Element} element - element to send keys to
         * @param {String} keys - keys to send to the element
         * @returns {Promise} fulfilled after keys are sent, or rejected in
         *                    the cases the WebDriver command errors
         */
        send_keys: function(element, keys) {
            if (!inView(element)) {
                element.scrollIntoView({behavior: "instant",
                                        block: "end",
                                        inline: "nearest"});
            }

            return window.test_driver_internal.send_keys(element, keys);
        },

        /**
         * Freeze the current page
         *
         * The freeze function transitions the page from the HIDDEN state to
         * the FROZEN state as described in `Lifecycle API for Web Pages
         * <https://github.com/WICG/page-lifecycle/blob/master/README.md>`_.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the freeze request is sent, or rejected
         *                    in case the WebDriver command errors
         */
        freeze: function(context=null) {
            return window.test_driver_internal.freeze();
        },

        /**
         * Minimizes the browser window.
         *
         * Matches the the behaviour of the `Minimize
         * <https://www.w3.org/TR/webdriver/#minimize-window>`_
         * WebDriver command
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled with the previous `WindowRect
         *                    <https://www.w3.org/TR/webdriver/#dfn-windowrect-object>`_
         *                    value, after the window is minimized.
         */
        minimize_window: function(context=null) {
            return window.test_driver_internal.minimize_window(context);
        },

        /**
         * Restore the window from minimized/maximized state to a given rect.
         *
         * Matches the behaviour of the `Set Window Rect
         * <https://www.w3.org/TR/webdriver/#set-window-rect>`_
         * WebDriver command
         *
         * @param {Object} rect - A `WindowRect
         *                        <https://www.w3.org/TR/webdriver/#dfn-windowrect-object>`_
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the window is restored to the given rect.
         */
        set_window_rect: function(rect, context=null) {
            return window.test_driver_internal.set_window_rect(rect, context);
        },

        /**
         * Send a sequence of actions
         *
         * This function sends a sequence of actions to perform.
         *
         * Matches the behaviour of the `Actions
         * <https://w3c.github.io/webdriver/#actions>`_ feature in
         * WebDriver.
         *
         * Authors are encouraged to use the
         * :js:class:`test_driver.Actions` builder rather than
         * invoking this API directly.
         *
         * @param {Array} actions - an array of actions. The format is
         *                          the same as the actions property
         *                          of the `Perform Actions
         *                          <https://w3c.github.io/webdriver/#perform-actions>`_
         *                          WebDriver command. Each element is
         *                          an object representing an input
         *                          source and each input source
         *                          itself has an actions property
         *                          detailing the behaviour of that
         *                          source at each timestep (or
         *                          tick). Authors are not expected to
         *                          construct the actions sequence by
         *                          hand, but to use the builder api
         *                          provided in testdriver-actions.js
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the actions are performed, or rejected in
         *                    the cases the WebDriver command errors
         */
        action_sequence: function(actions, context=null) {
            return window.test_driver_internal.action_sequence(actions, context);
        },

        /**
         * Generates a test report on the current page
         *
         * The generate_test_report function generates a report (to be
         * observed by ReportingObserver) for testing purposes.
         *
         * Matches the `Generate Test Report
         * <https://w3c.github.io/reporting/#generate-test-report-command>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the report is generated, or
         *                    rejected if the report generation fails
         */
        generate_test_report: function(message, context=null) {
            return window.test_driver_internal.generate_test_report(message, context);
        },

        /**
         * Sets the state of a permission
         *
         * This function causes permission requests and queries for the status
         * of a certain permission type (e.g. "push", or "background-fetch") to
         * always return ``state``.
         *
         * Matches the `Set Permission
         * <https://w3c.github.io/permissions/#set-permission-command>`_
         * WebDriver command.
         *
         * @example
         * await test_driver.set_permission({ name: "background-fetch" }, "denied");
         * await test_driver.set_permission({ name: "push", userVisibleOnly: true }, "granted");
         *
         * @param {PermissionDescriptor} descriptor - a `PermissionDescriptor
         *                              <https://w3c.github.io/permissions/#dom-permissiondescriptor>`_
         *                              or derived object.
         * @param {PermissionState} state - a `PermissionState
         *                          <https://w3c.github.io/permissions/#dom-permissionstate>`_
         *                          value.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         * @returns {Promise} fulfilled after the permission is set, or rejected if setting the
         *                    permission fails
         */
        set_permission: function(descriptor, state, context=null) {
            let permission_params = {
              descriptor,
              state,
            };
            return window.test_driver_internal.set_permission(permission_params, context);
        },

        /**
         * Creates a virtual authenticator
         *
         * This function creates a virtual authenticator for use with
         * the U2F and WebAuthn APIs.
         *
         * Matches the `Add Virtual Authenticator
         * <https://w3c.github.io/webauthn/#sctn-automation-add-virtual-authenticator>`_
         * WebDriver command.
         *
         * @param {Object} config - an `Authenticator Configuration
         *                          <https://w3c.github.io/webauthn/#authenticator-configuration>`_
         *                          object
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the authenticator is added, or
         *                    rejected in the cases the WebDriver command
         *                    errors. Returns the ID of the authenticator
         */
        add_virtual_authenticator: function(config, context=null) {
            return window.test_driver_internal.add_virtual_authenticator(config, context);
        },

        /**
         * Removes a virtual authenticator
         *
         * This function removes a virtual authenticator that has been
         * created by :js:func:`add_virtual_authenticator`.
         *
         * Matches the `Remove Virtual Authenticator
         * <https://w3c.github.io/webauthn/#sctn-automation-remove-virtual-authenticator>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator to be
         *                                    removed.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the authenticator is removed, or
         *                    rejected in the cases the WebDriver command
         *                    errors
         */
        remove_virtual_authenticator: function(authenticator_id, context=null) {
            return window.test_driver_internal.remove_virtual_authenticator(authenticator_id, context);
        },

        /**
         * Adds a credential to a virtual authenticator
         *
         * Matches the `Add Credential
         * <https://w3c.github.io/webauthn/#sctn-automation-add-credential>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator
         * @param {Object} credential - A `Credential Parameters
         *                              <https://w3c.github.io/webauthn/#credential-parameters>`_
         *                              object
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the credential is added, or
         *                    rejected in the cases the WebDriver command
         *                    errors
         */
        add_credential: function(authenticator_id, credential, context=null) {
            return window.test_driver_internal.add_credential(authenticator_id, credential, context);
        },

        /**
         * Gets all the credentials stored in an authenticator
         *
         * This function retrieves all the credentials (added via the U2F API,
         * WebAuthn, or the add_credential function) stored in a virtual
         * authenticator
         *
         * Matches the `Get Credentials
         * <https://w3c.github.io/webauthn/#sctn-automation-get-credentials>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the credentials are
         *                    returned, or rejected in the cases the
         *                    WebDriver command errors. Returns an
         *                    array of `Credential Parameters
         *                    <https://w3c.github.io/webauthn/#credential-parameters>`_
         */
        get_credentials: function(authenticator_id, context=null) {
            return window.test_driver_internal.get_credentials(authenticator_id, context=null);
        },

        /**
         * Remove a credential stored in an authenticator
         *
         * Matches the `Remove Credential
         * <https://w3c.github.io/webauthn/#sctn-automation-remove-credential>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator
         * @param {String} credential_id - the ID of the credential
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the credential is removed, or
         *                    rejected in the cases the WebDriver command
         *                    errors.
         */
        remove_credential: function(authenticator_id, credential_id, context=null) {
            return window.test_driver_internal.remove_credential(authenticator_id, credential_id, context);
        },

        /**
         * Removes all the credentials stored in a virtual authenticator
         *
         * Matches the `Remove All Credentials
         * <https://w3c.github.io/webauthn/#sctn-automation-remove-all-credentials>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the credentials are removed, or
         *                    rejected in the cases the WebDriver command
         *                    errors.
         */
        remove_all_credentials: function(authenticator_id, context=null) {
            return window.test_driver_internal.remove_all_credentials(authenticator_id, context);
        },

        /**
         * Sets the User Verified flag on an authenticator
         *
         * Sets whether requests requiring user verification will succeed or
         * fail on a given virtual authenticator
         *
         * Matches the `Set User Verified
         * <https://w3c.github.io/webauthn/#sctn-automation-set-user-verified>`_
         * WebDriver command.
         *
         * @param {String} authenticator_id - the ID of the authenticator
         * @param {boolean} uv - the User Verified flag
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         */
        set_user_verified: function(authenticator_id, uv, context=null) {
            return window.test_driver_internal.set_user_verified(authenticator_id, uv, context);
        },

        /**
         * Sets the storage access rule for an origin when embedded
         * in a third-party context.
         *
         * Matches the `Set Storage Access
         * <https://privacycg.github.io/storage-access/#set-storage-access-command>`_
         * WebDriver command.
         *
         * @param {String} origin - A third-party origin to block or allow.
         *                          May be "*" to indicate all origins.
         * @param {String} embedding_origin - an embedding (first-party) origin
         *                                    on which {origin}'s access should
         *                                    be blocked or allowed.
         *                                    May be "*" to indicate all origins.
         * @param {String} state - The storage access setting.
         *                         Must be either "allowed" or "blocked".
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the storage access rule has been
         *                    set, or rejected if setting the rule fails.
         */
        set_storage_access: function(origin, embedding_origin, state, context=null) {
            if (state !== "allowed" && state !== "blocked") {
                throw new Error("storage access status must be 'allowed' or 'blocked'");
            }
            const blocked = state === "blocked";
            return window.test_driver_internal.set_storage_access(origin, embedding_origin, blocked, context);
        },

        /**
         * Sets the current transaction automation mode for Secure Payment
         * Confirmation.
         *
         * This function places `Secure Payment
         * Confirmation <https://w3c.github.io/secure-payment-confirmation>`_ into
         * an automated 'autoaccept' or 'autoreject' mode, to allow testing
         * without user interaction with the transaction UX prompt.
         *
         * Matches the `Set SPC Transaction Mode
         * <https://w3c.github.io/secure-payment-confirmation/#sctn-automation-set-spc-transaction-mode>`_
         * WebDriver command.
         *
         * @example
         * await test_driver.set_spc_transaction_mode("autoaccept");
         * test.add_cleanup(() => {
         *   return test_driver.set_spc_transaction_mode("none");
         * });
         *
         * // Assumption: `request` is a PaymentRequest with a secure-payment-confirmation
         * // payment method.
         * const response = await request.show();
         *
         * @param {String} mode - The `transaction mode
         *                        <https://w3c.github.io/secure-payment-confirmation/#enumdef-transactionautomationmode>`_
         *                        to set. Must be one of "``none``",
         *                        "``autoaccept``", or
         *                        "``autoreject``".
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the transaction mode has been set,
         *                    or rejected if setting the mode fails.
         */
        set_spc_transaction_mode: function(mode, context=null) {
          return window.test_driver_internal.set_spc_transaction_mode(mode, context);
        },

        /**
         * Cancels the Federated Credential Management dialog
         *
         * Matches the `Cancel dialog
         * <https://fedidcg.github.io/FedCM/#webdriver-canceldialog>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the dialog is canceled, or rejected
         *                    in case the WebDriver command errors
         */
        cancel_fedcm_dialog: function(context=null) {
            return window.test_driver_internal.cancel_fedcm_dialog(context);
        },

        /**
         * Clicks a button on the Federated Credential Management dialog
         *
         * Matches the `Click dialog button
         * <https://fedidcg.github.io/FedCM/#webdriver-clickdialogbutton>`_
         * WebDriver command.
         *
         * @param {String} dialog_button - String enum representing the dialog button to click.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the button is clicked,
         *                    or rejected in case the WebDriver command errors
         */
        click_fedcm_dialog_button: function(dialog_button, context=null) {
          return window.test_driver_internal.click_fedcm_dialog_button(dialog_button, context);
        },

        /**
         * Selects an account from the Federated Credential Management dialog
         *
         * Matches the `Select account
         * <https://fedidcg.github.io/FedCM/#webdriver-selectaccount>`_
         * WebDriver command.
         *
         * @param {number} account_index - Index of the account to select.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the account is selected,
         *                    or rejected in case the WebDriver command errors
         */
        select_fedcm_account: function(account_index, context=null) {
          return window.test_driver_internal.select_fedcm_account(account_index, context);
        },

        /**
         * Gets the account list from the Federated Credential Management dialog
         *
         * Matches the `Account list
         * <https://fedidcg.github.io/FedCM/#webdriver-accountlist>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} fulfilled after the account list is returned, or
         *                    rejected in case the WebDriver command errors
         */
        get_fedcm_account_list: function(context=null) {
          return window.test_driver_internal.get_fedcm_account_list(context);
        },

        /**
         * Gets the title of the Federated Credential Management dialog
         *
         * Matches the `Get title
         * <https://fedidcg.github.io/FedCM/#webdriver-gettitle>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the title is returned, or rejected
         *                    in case the WebDriver command errors
         */
        get_fedcm_dialog_title: function(context=null) {
          return window.test_driver_internal.get_fedcm_dialog_title(context);
        },

        /**
         * Gets the type of the Federated Credential Management dialog
         *
         * Matches the `Get dialog type
         * <https://fedidcg.github.io/FedCM/#webdriver-getdialogtype>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the dialog type is returned, or
         *                    rejected in case the WebDriver command errors
         */
        get_fedcm_dialog_type: function(context=null) {
          return window.test_driver_internal.get_fedcm_dialog_type(context);
        },

        /**
         * Sets whether promise rejection delay is enabled for the Federated Credential Management dialog
         *
         * Matches the `Set delay enabled
         * <https://fedidcg.github.io/FedCM/#webdriver-setdelayenabled>`_
         * WebDriver command.
         *
         * @param {boolean} enabled - Whether to delay FedCM promise rejection.
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the delay has been enabled or disabled,
         *                    or rejected in case the WebDriver command errors
         */
        set_fedcm_delay_enabled: function(enabled, context=null) {
          return window.test_driver_internal.set_fedcm_delay_enabled(enabled, context);
        },

        /**
         * Resets the Federated Credential Management dialog's cooldown
         *
         * Matches the `Reset cooldown
         * <https://fedidcg.github.io/FedCM/#webdriver-resetcooldown>`_
         * WebDriver command.
         *
         * @param {WindowProxy} context - Browsing context in which
         *                                to run the call, or null for the current
         *                                browsing context.
         *
         * @returns {Promise} Fulfilled after the cooldown has been reset,
         *                    or rejected in case the WebDriver command errors
         */
        reset_fedcm_cooldown: function(context=null) {
          return window.test_driver_internal.reset_fedcm_cooldown(context);
        },

        /**
         * Creates a virtual sensor for use with the Generic Sensors APIs.
         *
         * Matches the `Create Virtual Sensor
         * <https://w3c.github.io/sensors/#create-virtual-sensor-command>`_
         * WebDriver command.
         *
         * Once created, a virtual sensor is available to all navigables under
         * the same top-level traversable (i.e. all frames in the same page,
         * regardless of origin).
         *
         * @param {String} sensor_type - A `virtual sensor type
         *                               <https://w3c.github.io/sensors/#virtual-sensor-metadata-virtual-sensor-type>`_
         *                               such as "accelerometer".
         * @param {Object} [sensor_params={}] - Optional parameters described
         *                                     in `Create Virtual Sensor
         *                                     <https://w3c.github.io/sensors/#create-virtual-sensor-command>`_.
         * @param {WindowProxy} [context=null] - Browsing context in which to
         *                                       run the call, or null for the
         *                                       current browsing context.
         *
         * @returns {Promise} Fulfilled when virtual sensor is created.
         *                    Rejected in case the WebDriver command errors out
         *                    (including if a virtual sensor of the same type
         *                    already exists).
         */
        create_virtual_sensor: function(sensor_type, sensor_params={}, context=null) {
          return window.test_driver_internal.create_virtual_sensor(sensor_type, sensor_params, context);
        },

        /**
         * Causes a virtual sensor to report a new reading to any connected
         * platform sensor.
         *
         * Matches the `Update Virtual Sensor Reading
         * <https://w3c.github.io/sensors/#update-virtual-sensor-reading-command>`_
         * WebDriver command.
         *
         * Note: The ``Promise`` it returns may fulfill before or after a
         * "reading" event is fired. When using
         * :js:func:`EventWatcher.wait_for`, it is necessary to take this into
         * account:
         *
         * Note: New values may also be discarded due to the checks in `update
         * latest reading
         * <https://w3c.github.io/sensors/#update-latest-reading>`_.
         *
         * @example
         * // Avoid races between EventWatcher and update_virtual_sensor().
         * // This assumes you are sure this reading will be processed (see
         * // the example below otherwise).
         * const reading = { x: 1, y: 2, z: 3 };
         * await Promise.all([
         *   test_driver.update_virtual_sensor('gyroscope', reading),
         *   watcher.wait_for('reading')
         * ]);
         *
         * @example
         * // Do not wait forever if you are not sure the reading will be
         * // processed.
         * const readingPromise = watcher.wait_for('reading');
         * const timeoutPromise = new Promise(resolve => {
         *     t.step_timeout(() => resolve('TIMEOUT', 3000))
         * });
         *
         * const reading = { x: 1, y: 2, z: 3 };
         * await test_driver.update_virtual_sensor('gyroscope', 'reading');
         *
         * const value =
         *     await Promise.race([timeoutPromise, readingPromise]);
         * if (value !== 'TIMEOUT') {
         *   // Do something. The "reading" event was fired.
         * }
         *
         * @param {String} sensor_type - A `virtual sensor type
         *                               <https://w3c.github.io/sensors/#virtual-sensor-metadata-virtual-sensor-type>`_
         *                               such as "accelerometer".
         * @param {Object} reading - An Object describing a reading in a format
         *                           dependent on ``sensor_type`` (e.g. ``{x:
         *                           1, y: 2, z: 3}`` or ``{ illuminance: 42
         *                           }``).
         * @param {WindowProxy} [context=null] - Browsing context in which to
         *                                       run the call, or null for the
         *                                       current browsing context.
         *
         * @returns {Promise} Fulfilled after the reading update reaches the
         *                    virtual sensor. Rejected in case the WebDriver
         *                    command errors out (including if a virtual sensor
         *                    of the given type does not exist).
         */
        update_virtual_sensor: function(sensor_type, reading, context=null) {
          return window.test_driver_internal.update_virtual_sensor(sensor_type, reading, context);
        },

        /**
         * Triggers the removal of a virtual sensor if it exists.
         *
         * Matches the `Delete Virtual Sensor
         * <https://w3c.github.io/sensors/#delete-virtual-sensor-command>`_
         * WebDriver command.
         *
         * @param {String} sensor_type - A `virtual sensor type
         *                               <https://w3c.github.io/sensors/#virtual-sensor-metadata-virtual-sensor-type>`_
         *                               such as "accelerometer".
         * @param {WindowProxy} [context=null] - Browsing context in which to
         *                                       run the call, or null for the
         *                                       current browsing context.
         *
         * @returns {Promise} Fulfilled after the virtual sensor has been
         *                    removed or if a sensor of the given type does not
         *                    exist. Rejected in case the WebDriver command
         *                    errors out.

         */
        remove_virtual_sensor: function(sensor_type, context=null) {
          return window.test_driver_internal.remove_virtual_sensor(sensor_type, context);
        },

        /**
         * Returns information about a virtual sensor.
         *
         * Matches the `Get Virtual Sensor Information
         * <https://w3c.github.io/sensors/#get-virtual-sensor-information-command>`_
         * WebDriver command.
         *
         * @param {String} sensor_type - A `virtual sensor type
         *                               <https://w3c.github.io/sensors/#virtual-sensor-metadata-virtual-sensor-type>`_
         *                               such as "accelerometer".
         * @param {WindowProxy} [context=null] - Browsing context in which to
         *                                       run the call, or null for the
         *                                       current browsing context.
         *
         * @returns {Promise} Fulfilled with an Object with the properties
         *                    described in `Get Virtual Sensor Information
         *                    <https://w3c.github.io/sensors/#get-virtual-sensor-information-command>`_.
         *                    Rejected in case the WebDriver command errors out
         *                    (including if a virtual sensor of the given type
         *                    does not exist).
         */
        get_virtual_sensor_information: function(sensor_type, context=null) {
            return window.test_driver_internal.get_virtual_sensor_information(sensor_type, context);
        }
    };

    window.test_driver_internal = {
        /**
         * This flag should be set to `true` by any code which implements the
         * internal methods defined below for automation purposes. Doing so
         * allows the library to signal failure immediately when an automated
         * implementation of one of the methods is not available.
         */
        in_automation: false,

        async click(element, coords) {
            if (this.in_automation) {
                throw new Error("click() is not implemented by testdriver-vendor.js");
            }

            return new Promise(function(resolve, reject) {
                element.addEventListener("click", resolve);
            });
        },

        async delete_all_cookies(context=null) {
            throw new Error("delete_all_cookies() is not implemented by testdriver-vendor.js");
        },

        async get_all_cookies(context=null) {
            throw new Error("get_all_cookies() is not implemented by testdriver-vendor.js");
        },

        async get_named_cookie(name, context=null) {
            throw new Error("get_named_cookie() is not implemented by testdriver-vendor.js");
        },

        async send_keys(element, keys) {
            if (this.in_automation) {
                throw new Error("send_keys() is not implemented by testdriver-vendor.js");
            }

            return new Promise(function(resolve, reject) {
                var seen = "";

                function remove() {
                    element.removeEventListener("keydown", onKeyDown);
                }

                function onKeyDown(event) {
                    if (event.key.length > 1) {
                        return;
                    }

                    seen += event.key;

                    if (keys.indexOf(seen) !== 0) {
                        reject(new Error("Unexpected key sequence: " + seen));
                        remove();
                    } else if (seen === keys) {
                        resolve();
                        remove();
                    }
                }

                element.addEventListener("keydown", onKeyDown);
            });
        },

        async freeze(context=null) {
            throw new Error("freeze() is not implemented by testdriver-vendor.js");
        },

        async minimize_window(context=null) {
            throw new Error("minimize_window() is not implemented by testdriver-vendor.js");
        },

        async set_window_rect(rect, context=null) {
            throw new Error("set_window_rect() is not implemented by testdriver-vendor.js");
        },

        async action_sequence(actions, context=null) {
            throw new Error("action_sequence() is not implemented by testdriver-vendor.js");
        },

        async generate_test_report(message, context=null) {
            throw new Error("generate_test_report() is not implemented by testdriver-vendor.js");
        },

        async set_permission(permission_params, context=null) {
            throw new Error("set_permission() is not implemented by testdriver-vendor.js");
        },

        async add_virtual_authenticator(config, context=null) {
            throw new Error("add_virtual_authenticator() is not implemented by testdriver-vendor.js");
        },

        async remove_virtual_authenticator(authenticator_id, context=null) {
            throw new Error("remove_virtual_authenticator() is not implemented by testdriver-vendor.js");
        },

        async add_credential(authenticator_id, credential, context=null) {
            throw new Error("add_credential() is not implemented by testdriver-vendor.js");
        },

        async get_credentials(authenticator_id, context=null) {
            throw new Error("get_credentials() is not implemented by testdriver-vendor.js");
        },

        async remove_credential(authenticator_id, credential_id, context=null) {
            throw new Error("remove_credential() is not implemented by testdriver-vendor.js");
        },

        async remove_all_credentials(authenticator_id, context=null) {
            throw new Error("remove_all_credentials() is not implemented by testdriver-vendor.js");
        },

        async set_user_verified(authenticator_id, uv, context=null) {
            throw new Error("set_user_verified() is not implemented by testdriver-vendor.js");
        },

        async set_storage_access(origin, embedding_origin, blocked, context=null) {
            throw new Error("set_storage_access() is not implemented by testdriver-vendor.js");
        },

        async set_spc_transaction_mode(mode, context=null) {
            throw new Error("set_spc_transaction_mode() is not implemented by testdriver-vendor.js");
        },

        async cancel_fedcm_dialog(context=null) {
            throw new Error("cancel_fedcm_dialog() is not implemented by testdriver-vendor.js");
        },

        async click_fedcm_dialog_button(dialog_button, context=null) {
            throw new Error("click_fedcm_dialog_button() is not implemented by testdriver-vendor.js");
        },

        async select_fedcm_account(account_index, context=null) {
            throw new Error("select_fedcm_account() is not implemented by testdriver-vendor.js");
        },

        async get_fedcm_account_list(context=null) {
            throw new Error("get_fedcm_account_list() is not implemented by testdriver-vendor.js");
        },

        async get_fedcm_dialog_title(context=null) {
            throw new Error("get_fedcm_dialog_title() is not implemented by testdriver-vendor.js");
        },

        async get_fedcm_dialog_type(context=null) {
            throw new Error("get_fedcm_dialog_type() is not implemented by testdriver-vendor.js");
        },

        async set_fedcm_delay_enabled(enabled, context=null) {
            throw new Error("set_fedcm_delay_enabled() is not implemented by testdriver-vendor.js");
        },

        async reset_fedcm_cooldown(context=null) {
            throw new Error("reset_fedcm_cooldown() is not implemented by testdriver-vendor.js");
        },

        async create_virtual_sensor(sensor_type, sensor_params, context=null) {
            throw new Error("create_virtual_sensor() is not implemented by testdriver-vendor.js");
        },

        async update_virtual_sensor(sensor_type, reading, context=null) {
            throw new Error("update_virtual_sensor() is not implemented by testdriver-vendor.js");
        },

        async remove_virtual_sensor(sensor_type, context=null) {
            throw new Error("remove_virtual_sensor() is not implemented by testdriver-vendor.js");
        },

        async get_virtual_sensor_information(sensor_type, context=null) {
            throw new Error("get_virtual_sensor_information() is not implemented by testdriver-vendor.js");
        }
    };
})();
