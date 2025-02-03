const executor_path = '/common/dispatcher/executor.html?pipe=';
const executor_worker_path = '/common/dispatcher/executor-worker.js?pipe=';
const executor_service_worker_path = '/common/dispatcher/executor-service-worker.js?pipe=';

// COEP
const coep_none =
    '|header(Cross-Origin-Embedder-Policy,none)';
const coep_credentialless =
    '|header(Cross-Origin-Embedder-Policy,credentialless)';
const coep_require_corp =
    '|header(Cross-Origin-Embedder-Policy,require-corp)';

// COEP-Report-Only
const coep_report_only_credentialless =
    '|header(Cross-Origin-Embedder-Policy-Report-Only,credentialless)';

// COOP
const coop_same_origin =
    '|header(Cross-Origin-Opener-Policy,same-origin)';

// CORP
const corp_cross_origin =
    '|header(Cross-Origin-Resource-Policy,cross-origin)';

const cookie_same_site_none = ';SameSite=None;Secure';

// Test using the modern async/await primitives are easier to read/write.
// However they run sequentially, contrary to async_test. This is the parallel
// version, to avoid timing out.
let promise_test_parallel = (promise, description) => {
  async_test(test => {
    promise(test)
      .then(() => test.done())
      .catch(test.step_func(error => { throw error; }));
  }, description);
};

// Add a cookie |cookie_key|=|cookie_value| on an |origin|.
// Note: cookies visibility depends on the path of the document. Those are set
// from a document from: /html/cross-origin-embedder-policy/credentialless/. So
// the cookie is visible to every path underneath.
const setCookie = async (origin, cookie_key, cookie_value) => {
  const popup_token = token();
  const popup_url = origin + executor_path + `&uuid=${popup_token}`;
  const popup = window.open(popup_url);

  const reply_token = token();
  send(popup_token, `
    document.cookie = "${cookie_key}=${cookie_value}";
    send("${reply_token}", "done");
  `);
  assert_equals(await receive(reply_token), "done");
  popup.close();
}

let parseCookies = function(headers_json) {
  if (!headers_json["cookie"])
    return {};

  return headers_json["cookie"]
    .split(';')
    .map(v => v.split('='))
    .reduce((acc, v) => {
      acc[v[0].trim()] = v[1].trim();
      return acc;
    }, {});
}

// Open a new window with a given |origin|, loaded with COEP:credentialless. The
// new document will execute any scripts sent toward the token it returns.
const newCredentiallessWindow = (origin) => {
  const main_document_token = token();
  const url = origin + executor_path + coep_credentialless +
    `&uuid=${main_document_token}`;
  const context = window.open(url);
  add_completion_callback(() => w.close());
  return main_document_token;
};

// Create a new iframe, loaded with COEP:credentialless.
// The new document will execute any scripts sent toward the token it returns.
const newCredentiallessIframe = (parent_token, child_origin) => {
  const sub_document_token = token();
  const iframe_url = child_origin + executor_path + coep_credentialless +
    `&uuid=${sub_document_token}`;
  send(parent_token, `
    let iframe = document.createElement("iframe");
    iframe.src = "${iframe_url}";
    document.body.appendChild(iframe);
  `)
  return sub_document_token;
};

// A common interface for building the 4 type of execution contexts:
// It outputs: [
//   - The token to communicate with the environment.
//   - A promise resolved when the environment encounters an error.
// ]
const environments = {
  document: headers => {
    const tok = token();
    const url = window.origin + executor_path + headers + `&uuid=${tok}`;
    const context = window.open(url);
    add_completion_callback(() => context.close());
    return [tok, new Promise(resolve => {})];
  },

  dedicated_worker: headers => {
    const tok = token();
    const url = window.origin + executor_worker_path + headers + `&uuid=${tok}`;
    const context = new Worker(url);
    return [tok, new Promise(resolve => context.onerror = resolve)];
  },

  shared_worker: headers => {
    const tok = token();
    const url = window.origin + executor_worker_path + headers + `&uuid=${tok}`;
    const context = new SharedWorker(url);
    return [tok, new Promise(resolve => context.onerror = resolve)];
  },

  service_worker: headers => {
    const tok = token();
    const url = window.origin + executor_worker_path + headers + `&uuid=${tok}`;
    const scope = url; // Generate a one-time scope for service worker.
    const error = new Promise(resolve => {
      navigator.serviceWorker.register(url, {scope: scope})
        .then(registration => {
          add_completion_callback(() => registration.unregister());
        }, /* catch */ resolve);
    });
    return [tok, error];
  },
};
