# slack-libpurple

A Slack protocol plugin for libpurple IM clients.

Here's how slack concepts are mapped to purple:

   * Your "open" channels (on the slack bar) are mapped to the buddy list: joining a channel is equivalent to creating a buddy
   * Which conversations are open in purple is up to you, and has no effect on slack... (how to deal with activity in open channels with no conversation?)
   * TBD... feedback welcome

## Installation/Configuration

1. Install libpurple (pidgin, finch, etc.), obviously
1. Install [json-parser](https://github.com/udp/json-parser) on your system
1. Run `make install` (or `make user-install`)
1. [Issue a Slack API token](https://api.slack.com/custom-integrations/legacy-tokens) for yourself
1. Create a slack account and enter this token (no password, hostname is optional)

## Status

- [x] Basic IM (direct message) functionality
- [x] Basic channel (chat) functionality
- [ ] Apply buddy changes to open/close channels
- [x] Proper message formatting (for @mentions and such, incoming only)
- [ ] Set presence/status
- [ ] Retrieve message history to populate new conversations?
- [ ] Optimize HTTP connections (libpurple 3?)
