const sigstore = require('sigstore')
const { readFile } = require('node:fs/promises')
const ci = require('ci-info')
const { env } = process

const INTOTO_PAYLOAD_TYPE = 'application/vnd.in-toto+json'
const INTOTO_STATEMENT_V01_TYPE = 'https://in-toto.io/Statement/v0.1'
const INTOTO_STATEMENT_V1_TYPE = 'https://in-toto.io/Statement/v1'
const SLSA_PREDICATE_V02_TYPE = 'https://slsa.dev/provenance/v0.2'
const SLSA_PREDICATE_V1_TYPE = 'https://slsa.dev/provenance/v1'

const GITHUB_BUILDER_ID_PREFIX = 'https://github.com/actions/runner'
const GITHUB_BUILD_TYPE = 'https://slsa-framework.github.io/github-actions-buildtypes/workflow/v1'

const GITLAB_BUILD_TYPE_PREFIX = 'https://github.com/npm/cli/gitlab'
const GITLAB_BUILD_TYPE_VERSION = 'v0alpha1'

const generateProvenance = async (subject, opts) => {
  let payload
  if (ci.GITHUB_ACTIONS) {
    /* istanbul ignore next - not covering missing env var case */
    const relativeRef = (env.GITHUB_WORKFLOW_REF || '').replace(env.GITHUB_REPOSITORY + '/', '')
    const delimiterIndex = relativeRef.indexOf('@')
    const workflowPath = relativeRef.slice(0, delimiterIndex)
    const workflowRef = relativeRef.slice(delimiterIndex + 1)

    payload = {
      _type: INTOTO_STATEMENT_V1_TYPE,
      subject,
      predicateType: SLSA_PREDICATE_V1_TYPE,
      predicate: {
        buildDefinition: {
          buildType: GITHUB_BUILD_TYPE,
          externalParameters: {
            workflow: {
              ref: workflowRef,
              repository: `${env.GITHUB_SERVER_URL}/${env.GITHUB_REPOSITORY}`,
              path: workflowPath,
            },
          },
          internalParameters: {
            github: {
              event_name: env.GITHUB_EVENT_NAME,
              repository_id: env.GITHUB_REPOSITORY_ID,
              repository_owner_id: env.GITHUB_REPOSITORY_OWNER_ID,
            },
          },
          resolvedDependencies: [
            {
              uri: `git+${env.GITHUB_SERVER_URL}/${env.GITHUB_REPOSITORY}@${env.GITHUB_REF}`,
              digest: {
                gitCommit: env.GITHUB_SHA,
              },
            },
          ],
        },
        runDetails: {
          builder: { id: `${GITHUB_BUILDER_ID_PREFIX}/${env.RUNNER_ENVIRONMENT}` },
          metadata: {
            /* eslint-disable-next-line max-len */
            invocationId: `${env.GITHUB_SERVER_URL}/${env.GITHUB_REPOSITORY}/actions/runs/${env.GITHUB_RUN_ID}/attempts/${env.GITHUB_RUN_ATTEMPT}`,
          },
        },
      },
    }
  }
  if (ci.GITLAB) {
    payload = {
      _type: INTOTO_STATEMENT_V01_TYPE,
      subject,
      predicateType: SLSA_PREDICATE_V02_TYPE,
      predicate: {
        buildType: `${GITLAB_BUILD_TYPE_PREFIX}/${GITLAB_BUILD_TYPE_VERSION}`,
        builder: { id: `${env.CI_PROJECT_URL}/-/runners/${env.CI_RUNNER_ID}` },
        invocation: {
          configSource: {
            uri: `git+${env.CI_PROJECT_URL}`,
            digest: {
              sha1: env.CI_COMMIT_SHA,
            },
            entryPoint: env.CI_JOB_NAME,
          },
          parameters: {
            CI: env.CI,
            CI_API_GRAPHQL_URL: env.CI_API_GRAPHQL_URL,
            CI_API_V4_URL: env.CI_API_V4_URL,
            CI_BUILD_BEFORE_SHA: env.CI_BUILD_BEFORE_SHA,
            CI_BUILD_ID: env.CI_BUILD_ID,
            CI_BUILD_NAME: env.CI_BUILD_NAME,
            CI_BUILD_REF: env.CI_BUILD_REF,
            CI_BUILD_REF_NAME: env.CI_BUILD_REF_NAME,
            CI_BUILD_REF_SLUG: env.CI_BUILD_REF_SLUG,
            CI_BUILD_STAGE: env.CI_BUILD_STAGE,
            CI_COMMIT_BEFORE_SHA: env.CI_COMMIT_BEFORE_SHA,
            CI_COMMIT_BRANCH: env.CI_COMMIT_BRANCH,
            CI_COMMIT_REF_NAME: env.CI_COMMIT_REF_NAME,
            CI_COMMIT_REF_PROTECTED: env.CI_COMMIT_REF_PROTECTED,
            CI_COMMIT_REF_SLUG: env.CI_COMMIT_REF_SLUG,
            CI_COMMIT_SHA: env.CI_COMMIT_SHA,
            CI_COMMIT_SHORT_SHA: env.CI_COMMIT_SHORT_SHA,
            CI_COMMIT_TIMESTAMP: env.CI_COMMIT_TIMESTAMP,
            CI_COMMIT_TITLE: env.CI_COMMIT_TITLE,
            CI_CONFIG_PATH: env.CI_CONFIG_PATH,
            CI_DEFAULT_BRANCH: env.CI_DEFAULT_BRANCH,
            CI_DEPENDENCY_PROXY_DIRECT_GROUP_IMAGE_PREFIX:
              env.CI_DEPENDENCY_PROXY_DIRECT_GROUP_IMAGE_PREFIX,
            CI_DEPENDENCY_PROXY_GROUP_IMAGE_PREFIX: env.CI_DEPENDENCY_PROXY_GROUP_IMAGE_PREFIX,
            CI_DEPENDENCY_PROXY_SERVER: env.CI_DEPENDENCY_PROXY_SERVER,
            CI_DEPENDENCY_PROXY_USER: env.CI_DEPENDENCY_PROXY_USER,
            CI_JOB_ID: env.CI_JOB_ID,
            CI_JOB_NAME: env.CI_JOB_NAME,
            CI_JOB_NAME_SLUG: env.CI_JOB_NAME_SLUG,
            CI_JOB_STAGE: env.CI_JOB_STAGE,
            CI_JOB_STARTED_AT: env.CI_JOB_STARTED_AT,
            CI_JOB_URL: env.CI_JOB_URL,
            CI_NODE_TOTAL: env.CI_NODE_TOTAL,
            CI_PAGES_DOMAIN: env.CI_PAGES_DOMAIN,
            CI_PAGES_URL: env.CI_PAGES_URL,
            CI_PIPELINE_CREATED_AT: env.CI_PIPELINE_CREATED_AT,
            CI_PIPELINE_ID: env.CI_PIPELINE_ID,
            CI_PIPELINE_IID: env.CI_PIPELINE_IID,
            CI_PIPELINE_SOURCE: env.CI_PIPELINE_SOURCE,
            CI_PIPELINE_URL: env.CI_PIPELINE_URL,
            CI_PROJECT_CLASSIFICATION_LABEL: env.CI_PROJECT_CLASSIFICATION_LABEL,
            CI_PROJECT_DESCRIPTION: env.CI_PROJECT_DESCRIPTION,
            CI_PROJECT_ID: env.CI_PROJECT_ID,
            CI_PROJECT_NAME: env.CI_PROJECT_NAME,
            CI_PROJECT_NAMESPACE: env.CI_PROJECT_NAMESPACE,
            CI_PROJECT_NAMESPACE_ID: env.CI_PROJECT_NAMESPACE_ID,
            CI_PROJECT_PATH: env.CI_PROJECT_PATH,
            CI_PROJECT_PATH_SLUG: env.CI_PROJECT_PATH_SLUG,
            CI_PROJECT_REPOSITORY_LANGUAGES: env.CI_PROJECT_REPOSITORY_LANGUAGES,
            CI_PROJECT_ROOT_NAMESPACE: env.CI_PROJECT_ROOT_NAMESPACE,
            CI_PROJECT_TITLE: env.CI_PROJECT_TITLE,
            CI_PROJECT_URL: env.CI_PROJECT_URL,
            CI_PROJECT_VISIBILITY: env.CI_PROJECT_VISIBILITY,
            CI_REGISTRY: env.CI_REGISTRY,
            CI_REGISTRY_IMAGE: env.CI_REGISTRY_IMAGE,
            CI_REGISTRY_USER: env.CI_REGISTRY_USER,
            CI_RUNNER_DESCRIPTION: env.CI_RUNNER_DESCRIPTION,
            CI_RUNNER_ID: env.CI_RUNNER_ID,
            CI_RUNNER_TAGS: env.CI_RUNNER_TAGS,
            CI_SERVER_HOST: env.CI_SERVER_HOST,
            CI_SERVER_NAME: env.CI_SERVER_NAME,
            CI_SERVER_PORT: env.CI_SERVER_PORT,
            CI_SERVER_PROTOCOL: env.CI_SERVER_PROTOCOL,
            CI_SERVER_REVISION: env.CI_SERVER_REVISION,
            CI_SERVER_SHELL_SSH_HOST: env.CI_SERVER_SHELL_SSH_HOST,
            CI_SERVER_SHELL_SSH_PORT: env.CI_SERVER_SHELL_SSH_PORT,
            CI_SERVER_URL: env.CI_SERVER_URL,
            CI_SERVER_VERSION: env.CI_SERVER_VERSION,
            CI_SERVER_VERSION_MAJOR: env.CI_SERVER_VERSION_MAJOR,
            CI_SERVER_VERSION_MINOR: env.CI_SERVER_VERSION_MINOR,
            CI_SERVER_VERSION_PATCH: env.CI_SERVER_VERSION_PATCH,
            CI_TEMPLATE_REGISTRY_HOST: env.CI_TEMPLATE_REGISTRY_HOST,
            GITLAB_CI: env.GITLAB_CI,
            GITLAB_FEATURES: env.GITLAB_FEATURES,
            GITLAB_USER_ID: env.GITLAB_USER_ID,
            GITLAB_USER_LOGIN: env.GITLAB_USER_LOGIN,
            RUNNER_GENERATE_ARTIFACTS_METADATA: env.RUNNER_GENERATE_ARTIFACTS_METADATA,
          },
          environment: {
            name: env.CI_RUNNER_DESCRIPTION,
            architecture: env.CI_RUNNER_EXECUTABLE_ARCH,
            server: env.CI_SERVER_URL,
            project: env.CI_PROJECT_PATH,
            job: {
              id: env.CI_JOB_ID,
            },
            pipeline: {
              id: env.CI_PIPELINE_ID,
              ref: env.CI_CONFIG_PATH,
            },
          },
        },
        metadata: {
          buildInvocationId: `${env.CI_JOB_URL}`,
          completeness: {
            parameters: true,
            environment: true,
            materials: false,
          },
          reproducible: false,
        },
        materials: [
          {
            uri: `git+${env.CI_PROJECT_URL}`,
            digest: {
              sha1: env.CI_COMMIT_SHA,
            },
          },
        ],
      },
    }
  }
  return sigstore.attest(Buffer.from(JSON.stringify(payload)), INTOTO_PAYLOAD_TYPE, opts)
}

const verifyProvenance = async (subject, provenancePath) => {
  let provenanceBundle
  try {
    provenanceBundle = JSON.parse(await readFile(provenancePath))
  } catch (err) {
    err.message = `Invalid provenance provided: ${err.message}`
    throw err
  }

  const payload = extractProvenance(provenanceBundle)
  if (!payload.subject || !payload.subject.length) {
    throw new Error('No subject found in sigstore bundle payload')
  }
  if (payload.subject.length > 1) {
    throw new Error('Found more than one subject in the sigstore bundle payload')
  }

  const bundleSubject = payload.subject[0]
  if (subject.name !== bundleSubject.name) {
    throw new Error(
      `Provenance subject ${bundleSubject.name} does not match the package: ${subject.name}`
    )
  }
  if (subject.digest.sha512 !== bundleSubject.digest.sha512) {
    throw new Error('Provenance subject digest does not match the package')
  }

  await sigstore.verify(provenanceBundle)
  return provenanceBundle
}

const extractProvenance = (bundle) => {
  if (!bundle?.dsseEnvelope?.payload) {
    throw new Error('No dsseEnvelope with payload found in sigstore bundle')
  }
  try {
    return JSON.parse(Buffer.from(bundle.dsseEnvelope.payload, 'base64').toString('utf8'))
  } catch (err) {
    err.message = `Failed to parse payload from dsseEnvelope: ${err.message}`
    throw err
  }
}

module.exports = {
  generateProvenance,
  verifyProvenance,
}
