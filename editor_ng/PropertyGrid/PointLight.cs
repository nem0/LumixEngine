using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Drawing;
using System.Data;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;

namespace editor_ng.PropertyGrid
{
    public partial class PointLight : UserControl
    {
        native.EditorServer m_editor_server;

        public PointLight(native.EditorServer server)
        {
            m_editor_server = server;
            InitializeComponent();
            m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
            this.VisibleChanged += (object sender, EventArgs e) =>
            {
                m_editor_server.onComponentProperties -= m_editor_server_onComponentProperties;
                if (Visible)
                {
                    m_editor_server.onComponentProperties += m_editor_server_onComponentProperties;
                }
            };
        }

        void m_editor_server_onComponentProperties(object sender, EventArgs e)
        {
            BeginInvoke(new Action(() =>
            {
                native.EditorServer.ComponentPropertiesArgs args = e as native.EditorServer.ComponentPropertiesArgs;
                for (int i = 0; i < args.names.Length; ++i)
                {
                    if (args.names[i] == "radius")
                    {
                        radiusNumericUpDown.Value = (decimal)float.Parse(args.values[i].Replace('.', ','));
                    }
                    else if (args.names[i] == "fov")
                    {
                        fovNumericUpDown.Value = (decimal)float.Parse(args.values[i].Replace('.', ','));
                    }
                }
            }));
        }

        private void radiusNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("point_light", "radius", radiusNumericUpDown.Value.ToString());
        }

        private void fovNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("point_light", "fov", fovNumericUpDown.Value.ToString());
        }

        private void sizeNumericUpDown_ValueChanged(object sender, EventArgs e)
        {

        }
    }
}
