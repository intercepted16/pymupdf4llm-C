import os
import sys

class suppress_output:
    def __enter__(self):
        # Save original file descriptors
        self.stdout_fd = sys.stdout.fileno()
        self.stderr_fd = sys.stderr.fileno()

        # Save copies of original fds
        self.saved_stdout_fd = os.dup(self.stdout_fd)
        self.saved_stderr_fd = os.dup(self.stderr_fd)

        # Redirect to devnull
        self.devnull = os.open(os.devnull, os.O_WRONLY)
        os.dup2(self.devnull, self.stdout_fd)
        os.dup2(self.devnull, self.stderr_fd)
    
    def __exit__(self, exc_type, exc_val, exc_tb):
        # Restore original fds
        os.dup2(self.saved_stdout_fd, self.stdout_fd)
        os.dup2(self.saved_stderr_fd, self.stderr_fd)

        # Close fds
        os.close(self.devnull)
        os.close(self.saved_stdout_fd)
        os.close(self.saved_stderr_fd)