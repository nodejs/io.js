import { before, mock, test } from 'node:test';
import { strictEqual } from 'node:assert';

const logger = mock.fn((s) => console.log(`Hello, ${s}!`));

before(async () => {
    mock.module('./module-logger.ts', {
        namedExports: { logger }
    });
    mock.module('./commonjs-logger.ts', {
        namedExports: { logger }
    });
});

test('logger', async () => {
    const { logger: mockedModule } = await import('./module-logger.ts');
    mockedModule('TypeScript-Module');
    strictEqual(logger.mock.callCount(), 1);

    const { logger: mockedCommonjs } = await import('./commonjs-logger.ts');
    mockedCommonjs('TypeScript-CommonJS');
    strictEqual(logger.mock.callCount(), 2);
});
