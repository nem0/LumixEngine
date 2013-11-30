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
        };

        Notifications m_notifications;
        List<CompilationProcess> m_processes = new List<CompilationProcess>();


        public event EventHandler onAllScriptsCompiled;

        public ScriptCompiler(Notifications notifications)
        {
            m_notifications = notifications;
        }


        public void compileAllScripts()
        {
            DirectoryInfo dir_info = new DirectoryInfo("scripts");
            foreach (FileInfo file in dir_info.GetFiles("*.cpp"))
            {
                CompilationProcess process = new CompilationProcess();
                process.script_name = file.Name;
                process.process = new Process();
                ProcessStartInfo start_info = new ProcessStartInfo();
                process.process.StartInfo.FileName = "cmd.exe";
                process.process.StartInfo.Arguments = "/C " + System.IO.Directory.GetCurrentDirectory() + "\\scripts\\compile.bat " + file.FullName;
                process.process.StartInfo.CreateNoWindow = true;
                process.process.StartInfo.UseShellExecute = false;
                process.process.StartInfo.RedirectStandardOutput = true;
                process.process.StartInfo.Verb = "execute";
                process.process.Exited += process_Exited;
                process.process.EnableRaisingEvents = true;
                m_processes.Add(process);
                process.process.Start();
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
                    if (p.process.ExitCode != 0)
                    {
                        //string output = process.StandardOutput.ReadToEnd();
                        //error notification does not work here
                        m_notifications.invokeNotification("Script " + p.script_name + " failed to compile");
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
