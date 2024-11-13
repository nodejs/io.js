export interface OptionsBinding {
  getOptions(): {
    options: Map<
      string,
      {
        helpText: string;
        envVarSettings: 0 | 1;
      } & (
        | { type: 0 | 1; value: undefined }
        | { type: 2; value: boolean }
        | { type: 3 | 4; value: number }
        | { type: 5; value: string }
        | { type: 6; value: { host: string; port: number } }
        | { type: 7; value: string[] }
      )
    >;
    aliases: Map<string, string[]>;
  };
  envSettings: {
    kAllowedInEnvvar: 0;
    kDisallowedInEnvvar: 1;
  };
  noGlobalSearchPaths: boolean;
  shouldNotRegisterESMLoader: boolean;
  types: {
    kNoOp: 0;
    kV8Option: 1;
    kBoolean: 2;
    kInteger: 3;
    kUInteger: 4;
    kString: 5;
    kHostPort: 6;
    kStringList: 7;
  };
}
