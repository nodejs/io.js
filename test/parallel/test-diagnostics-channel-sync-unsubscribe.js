'use strict';

const common = require('../common');
const dc = require('node:diagnostics_channel');

const channel_name = 'test:channel';
const published_data = 'some message';

const onMessageHandler = common.mustCall(() => dc.unsubscribe(channel_name, onMessageHandler));

dc.subscribe(channel_name, onMessageHandler);
dc.subscribe(channel_name, common.mustCall());

// This must not throw.
dc.channel(channel_name).publish(published_data);
