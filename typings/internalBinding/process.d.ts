interface CpuUsageValue { 
  user: number;
  system: number;
}

declare namespace InternalProcessBinding {
  interface Process {
    cpuUsage(previousValue?: CpuUsageValue): CpuUsageValue;
    threadCpuUsage(previousValue?: CpuUsageValue): CpuUsageValue;
  }
}

export interface ProcessBinding {
  process: InternalProcessBinding.Process;
}
