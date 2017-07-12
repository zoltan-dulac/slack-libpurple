# slack-libpurple

A Slack protocol plugin for libpurple IM clients.

Here's how slack concepts are mapped to purple:

   * Your "open" channels (on the slack bar) are mapped to the buddy list: joining a channel is equivalent to creating a buddy
   * Which conversations are open in purple is up to you, and has no effect on slack... (how to deal with activity in open channels with no conversation?)
   * TBD... feedback welcome

## Installation/Configuration

1. Install libpurple (pidgin, finch, etc.), obviously
1. Install json-parser on your system from https://github.com/udp/json-parser.
1. Run `make install` (or `make user-install`)
1. Go to `https://api.slack.com/custom-integrations/legacy-tokens` and issue a token for yourself
1. Create a slack account and enter this token (no password)

## Status

- [x] Basic IM (direct message) functionality
- [ ] Basic channel (chat) functionality
- [ ] Apply buddy changes to open/close channels
- [ ] Proper message formatting (for @mentions and such)
- [ ] Set presence/status
- [ ] Retrieve message history to populate new conversations?
- [ ] Optimize HTTP connections (libpurple 3?)
