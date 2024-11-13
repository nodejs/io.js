# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from requests.exceptions import HTTPError

from blinkpy.w3c.test_exporter import TestExporter
from blinkpy.w3c.wpt_github import GitHubError

import logging

_log = logging.getLogger(__name__)

PR_APPROVAL_LOGIN = 'test262-merge-bot'


class BaseV8TestExporter(TestExporter):

  def export_in_flight_changes(self, pr_events) -> bool:
    """Test262 exporter ignores in-flight changes."""
    pass


class V8TestExporter(BaseV8TestExporter):

  def merge_pull_request(self, pull_request):
    """Exporter mode does not merge any PRs. We only create them."""
    pass


class V8TestApprover(BaseV8TestExporter):

  def merge_pull_request(self, pull_request):
    """Merges a pull request only if in approver mode."""
    self.approve(pull_request.number)
    super().merge_pull_request(pull_request)

  def approve(self, pr_number):
    """Approves a PR.
        Chromium/WPT exports are self approved via a web hook  with self
        approve functionality from an external github project
        (https://github.com/web-platform-tests/wpt-pr-bot).

        We will mimic the same behavior by sending an approve request just
        before we attempt a second pass merge.
        """
    if not self.is_approvable(pr_number):
      return

    body = {
        'body': ('The review process for this patch is being conducted in '
            'the V8 project.'),
        'event': 'APPROVE',
        'comments': [],
    }

    try:
      response = self.github.request(
          self.pr_path(pr_number), method='POST', body=body)
    except HTTPError as e:
      response = e.response
      _log.error('Failed to approve PR %d: %s', pr_number, response.text)

  def pr_path(self, pr_number):
    return (f'/repos/{self.github.gh_org}/{self.github.gh_repo_name}'
            f'/pulls/{pr_number}/reviews')

  def is_approvable(self, pr_number):
    """Validates approval requirements."""
    try:
      response = self.github.request(self.pr_path(pr_number), method='GET')
      reviews = response.data
      reviewed_by_bot = any(
          review['user']['login'] == PR_APPROVAL_LOGIN for review in reviews)
      if reviewed_by_bot:
        _log.info('PR %d already approved by us', pr_number)
        return False
    except HTTPError as e:
      response = e.response
      _log.error('Failed to get PR %d: %s', pr_number, response.text)
      return False
    return True

  def create_or_update_pr_from_landed_commit(self, commit):
    """Approver mode does not create any PRs"""
    pass
