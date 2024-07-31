/*
 * Polyfill for attaching shadow trees for declarative Shadow DOM for
 * implementations that do not support declarative Shadow DOM.
 *
 * Note: this polyfill will feature-detect the native feature, and do nothing
 *       if supported.
 *
 * See: https://github.com/whatwg/html/pull/5465
 *
 * root: The root of the subtree in which to upgrade shadow roots
 *
 */

function polyfill_declarative_shadow_dom(root) {
  if (HTMLTemplateElement.prototype.hasOwnProperty('shadowRootMode'))
    return;
  root.querySelectorAll("template[shadowrootmode]").forEach(template => {
    const mode = template.getAttribute("shadowrootmode");
    const delegatesFocus = template.hasAttribute("shadowrootdelegatesfocus");
    const shadowRoot = template.parentNode.attachShadow({ mode, delegatesFocus });
    shadowRoot.appendChild(template.content);
    template.remove();
    polyfill_declarative_shadow_dom(shadowRoot);
  });
}
