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
                    if (args.names[i] == Crc32.Compute("radius"))
                    {
                        radiusNumericUpDown.Value = (decimal)BitConverter.ToSingle(args.values[i], 0);
                    }
                    else if (args.names[i] == Crc32.Compute("fov"))
                    {
                        fovNumericUpDown.Value = (decimal)BitConverter.ToSingle(args.values[i], 0);
                    }
                }
            }));
        }

        private void radiusNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("point_light", "radius", radiusNumericUpDown.Value);
        }

        private void fovNumericUpDown_ValueChanged(object sender, EventArgs e)
        {
            m_editor_server.setComponentProperty("point_light", "fov", fovNumericUpDown.Value);
        }

        private void sizeNumericUpDown_ValueChanged(object sender, EventArgs e)
        {

        }
    }
}
