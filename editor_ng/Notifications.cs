using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace editor_ng
{
    class Notifications
    {
        MainForm m_main_form;
        List<Panel> m_panels = new List<Panel>();

        public Notifications(MainForm main_form)
        {
            m_main_form = main_form;
        }

        private const int PANEL_HEIGHT = 50;
        private const int PANEL_MARGIN = 10;

        public void showNotification(string text)
        {
            System.Timers.Timer timer = new System.Timers.Timer();
            timer.Elapsed += timer_Elapsed;
            timer.Interval = 5000;
            timer.Enabled = true;

            Panel p = new Panel();
            p.BorderStyle = BorderStyle.FixedSingle;
            p.Width = 200;
            p.Height = PANEL_HEIGHT;
            Label t = new Label();
            t.Dock = DockStyle.Fill;
            t.Text = text;
            p.Controls.Add(t);
            m_main_form.Controls.Add(p);
            p.Left = m_main_form.ClientSize.Width - 250;
            p.Top = m_main_form.ClientSize.Height - (PANEL_HEIGHT + PANEL_MARGIN) * (1 + m_panels.Count);
            p.BringToFront();
            m_panels.Add(p);
        }

        public void invokeNotification(string text)
        {
            IAsyncResult res = m_main_form.BeginInvoke(new Action(() => {
               showNotification(text);
            }));
            m_main_form.EndInvoke(res);
        }

        void timer_Elapsed(object sender, System.Timers.ElapsedEventArgs e)
        {
            (sender as System.Timers.Timer).Enabled = false;
            m_main_form.BeginInvoke(new Action(()=>{
                m_panels.First().Parent.Controls.Remove(m_panels.First());
            
                m_panels.RemoveAt(0);
                foreach (Panel p in m_panels)
                {
                    p.Top = p.Top + PANEL_HEIGHT + PANEL_MARGIN;
                }
            }));
        }
    }
}
