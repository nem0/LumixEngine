using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.IO;
using System.Diagnostics;

namespace editor_ng
{
    class ScriptCompiler
    {
        private class CompilationProcess
        {
            public Process process;
            public string script_name;
            public event EventHandler onCompiled;

            public void compiled()
            {
                if (onCompiled != null)
                {
                    onCompiled(this, null);
                }
            }
        };

        private List<CompilationProcess> m_processes = new List<CompilationProcess>();

        public class CompileErrorEventArgs : EventArgs
        {
            public string error_message;
            public string script_name;
        };

        public class ScriptUpdatedEventArgs : EventArgs
        {
            public string script_name;
        };

        public event EventHandler onCompileError;
        public event EventHandler onAllScriptsCompiled;
        public event EventHandler onScriptUpdated;

        void process_onCompiled(object sender, EventArgs e)
        {
            if (onScriptUpdated != null)
            {
                CompilationProcess process = sender as CompilationProcess;
                ScriptUpdatedEventArgs args = new ScriptUpdatedEventArgs();
                args.script_name = process.script_name;
                onScriptUpdated(this, args);
            }
        }

        public void compile(string path, string filename, bool emit_event)
        {
            CompilationProcess p = new CompilationProcess();
            if (emit_event)
            {
                p.onCompiled += process_onCompiled;
            }
            compile(p, path, filename);
        }

        private void compile(CompilationProcess process, string path, string filename)
        {
            process.script_name = filename;
            process.process = new Process();
            ProcessStartInfo start_info = new ProcessStartInfo();
            process.process.StartInfo.FileName = "cmd.exe";
            process.process.StartInfo.Arguments = "/C " + System.IO.Directory.GetCurrentDirectory() + "\\scripts\\compile.bat " + path;
            process.process.StartInfo.CreateNoWindow = true;
            process.process.StartInfo.UseShellExecute = false;
            process.process.StartInfo.RedirectStandardOutput = true;
            process.process.StartInfo.Verb = "execute";
            process.process.Exited += process_Exited;
            process.process.EnableRaisingEvents = true;
            m_processes.Add(process);
            process.process.Start();
        }

        public void compileAllScripts()
        {
            DirectoryInfo dir_info = new DirectoryInfo("scripts");
            foreach (FileInfo file in dir_info.GetFiles("*.cpp"))
            {
                compile(new CompilationProcess(), file.FullName, file.Name);   
            }
        }

        public bool areAllScriptCompiled()
        {
            return m_processes.Count == 0;
        }

        void process_Exited(object sender, EventArgs e)
        {
            foreach (CompilationProcess p in m_processes)
            {
                if (p.process == sender)
                {
                    p.compiled();
                    if (p.process.ExitCode != 0 && onCompileError != null)
                    {
                        CompileErrorEventArgs args = new CompileErrorEventArgs();
                        args.error_message = p.process.StandardOutput.ReadToEnd();
                        args.script_name = p.script_name;
                        onCompileError(this, args);
                    }
                    m_processes.Remove(p);
                    if (m_processes.Count == 0 && onAllScriptsCompiled != null)
                    {
                        onAllScriptsCompiled(this, null);
                    }

                    break;
                }
            }
        }
    }
}
