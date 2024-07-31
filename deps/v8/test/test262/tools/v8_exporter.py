# Copyright 2023 the V8 project authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

from requests.exceptions import HTTPError

from blinkpy.w3c.test_exporter import TestExporter
from blinkpy.w3c.wpt_github import GitHubError

import logging

_log = logging.getLogger(__name__)

class V8TestExporter(TestExporter):
    def merge_pull_request(self, pull_request):
        """Exporter mode does not merge any PRs. We only create them."""
        pass


class V8TestApprover(TestExporter):
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
        path = '/repos/%s/%s/pulls/%d/reviews'  % (
            self.github.gh_org,
            self.github.gh_repo_name,
            pr_number)

        body = {
            'body': ('The review process for this patch is being conducted in '
                'the V8 project.'),
            'event': 'APPROVE',
            'comments': [],
        }

        try:
            response = self.github.request(path, method='POST', body=body)
        except HTTPError as e:
            response = e.response
            _log.error('Failed to approve PR %d: %s', pr_number, response.text)

    def create_or_update_pr_from_landed_commit(self, commit):
        """Approver mode does not create any PRs"""
        pass
