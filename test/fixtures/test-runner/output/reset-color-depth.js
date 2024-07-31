'use strict';

// Make tests OS-independent by overriding stdio getColorDepth().
process.stdout.getColorDepth = () => 8;
process.stderr.getColorDepth = () => 8;
