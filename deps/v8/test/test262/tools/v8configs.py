# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Contains all project-specific configuration and entities for V8/Tets262 host."""

import json

from functools import cached_property

from blinkpy.w3c.local_wpt import LocalRepo
from blinkpy.w3c.wpt_github import GitHubRepo, PROVISIONAL_PR_LABEL
from blinkpy.w3c.chromium_configs import ProjectConfig


def to_object(item):
  """
  Recursively convert a dictionary to an object.
  """
  if isinstance(item, dict):
    return type('_dict_obj', (), {k: to_object(v) for k, v in item.items()})
  if isinstance(item, list):
    return [to_object(element) for element in item]
  return item


class Test262GitHub(GitHubRepo):

  def __init__(self,
               config,
               host,
               user=None,
               token=None,
               pr_history_window=1000):
    super(Test262GitHub, self).__init__(
        gh_org=config.github.org,
        gh_repo_name=config.github.repo,
        export_pr_label=config.exporter.pr_label,
        provisional_pr_label=PROVISIONAL_PR_LABEL,
        host=host,
        user=user,
        token=token,
        pr_history_window=pr_history_window,
        main_branch=config.github.main_branch,
        min_expected_prs=0)

  @property
  def skipped_revisions(self):
    return [
        # Revisions below would choke on github checks. Next revision in v8 repo
        # is an effective reland.
        "f4e862d29f6aeaa66b5e8e755dc70fe7cb2bc243",
        "44702d6ea9c1bce9c95316e3aca41c14e0e8533f",
        "87437c41113c2b41aaf5bcbc7fee996df0ab1711",
        "b8d49712ab12dd13bee5da34849f8ab7f5309715",
        "46c933180967191758afd0427f9d0ba02a4d24a3",
        "85bf69fd70e118b1ace64110ed82cb1ff0d58533",
        "b97d50ae5e4bb6b5abfc811c0b618d888c3f1377",
        "c4946b3e60c597fb9d9cccb2b100647a945e309d",
    ]


class V8Test262Config(ProjectConfig):

  def __init__(self, filesystem, config_file):
    super().__init__(filesystem)
    with open(config_file, 'r') as f:
      self.config = to_object(json.loads(f.read()))
    self.relative_tests_path = self.config.gerrit.relative_tests_path
    self.revision_footer = self.config.gerrit.revision_footer
    self.gerrit_project = self.config.gerrit.project
    self.gerrit_branch = self.config.gerrit.branch
    self.github_factory = github_factory(self.config)
    self.local_repo_factory = local_repo_factory(self.config)
    self.files_to_copy = self.config.importer.files_to_copy
    self.paths_to_sync = self.config.importer.paths_to_sync

  @cached_property
  def project_root(self):
    from blinkpy.common.system.executive import Executive
    return Executive().run_command(
        ['git', 'rev-parse', '--show-toplevel']).strip()

  @property
  def test_root(self):
    return self.project_root + '/' + self.relative_tests_path

  @property
  def import_destination_path(self):
    return self.test_root

  @property
  def pr_updated_comment_template(self):
    return self.config.exporter.pr_updated_comment

  def inflight_cl_comment_template(self):
    return self.config.exporter.inflight_cl_comment


def config_from_file(config_file):
  return lambda filesystem: V8Test262Config(filesystem, config_file)


def local_repo_factory(config):
  return lambda host, gh_token: LocalRepo(
      name=config.github.name,
      gh_org=config.github.org,
      gh_repo_name=config.github.repo,
      gh_ssh_url_template='https://{}@github.com/%s/%s.git' %
      (config.github.org, config.github.repo),
      mirror_url=None,
      default_committer_email=config.exporter.committer_email,
      default_committer_name=config.exporter.committer_name,
      patch_path_renames=[(r.source, r.destination)
                          for r in config.github.patch_path_renames],
      host=host,
      gh_token=gh_token,
      main_branch=config.github.main_branch,
  )


def github_factory(config):
  return lambda host, user, token: Test262GitHub(config, host, user, token)
