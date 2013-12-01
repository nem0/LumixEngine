using System;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Runtime.InteropServices;
using System.Text;
using System.Threading.Tasks;
using System.Net.Sockets;
using System.Net; 


namespace editor_ng.native
{
    public class EditorServer
    {
        [DllImport("engine.dll")]
        static extern IntPtr luxServerInit(IntPtr hwnd, IntPtr game_hwnd, IntPtr base_path);
        [DllImport("engine.dll")]
        static extern void luxServerTick(IntPtr server);
        [DllImport("engine.dll")]
        static extern void luxServerResize(IntPtr server);

        private IntPtr m_server = IntPtr.Zero;
        private byte[] m_buffer;
        private TcpClient m_client;

        public class EntityPositionArgs : EventArgs
        {
            public int uid;
            public float x;
            public float y;
            public float z;
        };
        public event EventHandler onEntityPosition;

        public class EntitySelectedArgs : EventArgs
        {
            public int uid;
            public uint[] cmps;
        };
        public event EventHandler onEntitySelected;

        public class ComponentPropertiesArgs : EventArgs
        {
            public uint cmp_type;
            public string[] names;
            public string[] values;
            public uint[] types;
        };
        public event EventHandler onComponentProperties;

        public void create(IntPtr hwnd, IntPtr game_hwnd, string base_path)
        {
            IntPtr strPtr = Marshal.StringToHGlobalAnsi(base_path);
            m_server = luxServerInit(hwnd, game_hwnd, strPtr);
            Marshal.FreeHGlobal(strPtr);

            m_buffer = new byte[1024*256];
            System.Threading.Thread.Sleep(1000); // waiting for native part /// TODO remove this hack
            m_client = new TcpClient("127.0.0.1", 10002);
            m_client.GetStream().BeginRead(m_buffer, 0, 1024*256, new AsyncCallback(onReceive), this);
        }

        private int m_pos = 0;

        private void handleMessage(int read)
        {
            m_pos += read;

            if (m_pos > 8)
            {
                if (BitConverter.ToInt32(m_buffer, 4) == 0x12345678)
                {
                    int len = BitConverter.ToInt32(m_buffer, 0);
                    if (m_pos < 8 + len)
                    {
                        return;
                    }

                    using (MemoryStream memory = new MemoryStream(m_buffer, 8, len + 8))
                    {
                        using (BinaryReader reader = new BinaryReader(memory))
                        {
                            int type = reader.ReadInt32();
                            switch (type)
                            {
                                case 1:
                                    entitySelectedCallback(reader);
                                    break;
                                case 2:
                                    componentPropertiesCallback(reader);
                                    break;
                                case 3:
                                    entityPositionCallback(reader);
                                    break;
                                case 4:
                                    logMessage(reader);
                                    break;
                            }
                        }
                    }
                    if (m_pos > 8 + len)
                    {
                        Array.Copy(m_buffer, 8 + len, m_buffer, 0, m_pos - len - 8);
                    }
                    m_pos -= 8 + len;
                    if (m_pos >= 8)
                    {
                        handleMessage(0);
                    }
                }
            }
        }

        private void onReceive(IAsyncResult result)
        {
            int read = (result.AsyncState as EditorServer).m_client.GetStream().EndRead(result);

            if (read > 0)
            {
                handleMessage(read);
            }
            m_client.GetStream().BeginRead(m_buffer, m_pos, 256 * 1024 - m_pos, new AsyncCallback(onReceive), this);
        }

        private void sendMessage(System.IO.MemoryStream stream)
        {
            m_client.GetStream().Write(BitConverter.GetBytes(stream.Length), 0, 4);
            m_client.GetStream().Write(BitConverter.GetBytes(0), 0, 1);
            m_client.GetStream().Write(stream.GetBuffer(), 0, (int)stream.Length);
            //luxServerMessage(m_server, ptr, length);
        }

        private MemoryStream m_stream = null;
        private BinaryWriter m_writer = null;

        private void startMessage()
        {
            if (m_stream != null)
                throw new Exception("Message already started");
            m_stream = new MemoryStream();
            m_writer = new BinaryWriter(m_stream);
        }

        private void sendMessage()
        {
            if (m_stream == null)
                throw new Exception("No message");
            MemoryStream stream = m_stream;
            m_stream = null;
            m_writer = null;
            sendMessage(stream);
        }

        public void resize()
        {
            luxServerResize(m_server);
        }

        public void draw()
        {
            luxServerTick(m_server);
        }

        public void createComponent(uint cmp)
        {
            startMessage();
            m_writer.Write(8);
            m_writer.Write(cmp);
            m_writer.Write(0);
            sendMessage();
        }

        public void requestPosition()
        {
            startMessage();
            m_writer.Write(13);
            m_writer.Write(0);
            sendMessage();
        }

        public void setComponentProperty(uint cmp, string name, string value)
        {
            startMessage();
            m_writer.Write(4);
            m_writer.Write(cmp);
            m_writer.Write(name.Length);
            m_writer.Write(System.Text.Encoding.ASCII.GetBytes(name));
            m_writer.Write(value.Length);
            m_writer.Write(System.Text.Encoding.ASCII.GetBytes(value));
            sendMessage();
        }

        public void openUniverse(string filename)
        {
            startMessage();
            m_writer.Write(7);
            m_writer.Write(System.Text.Encoding.ASCII.GetBytes(filename));
            m_writer.Write(0);
            sendMessage();
        }

        public void mouseDown(int x, int y, int button)
        {
            startMessage();
            m_writer.Write(1);
            m_writer.Write(x);
            m_writer.Write(y);
            m_writer.Write(button);
            sendMessage();
        }

        public void mouseMove(int x, int y, int dx, int dy, int button)
        {
            startMessage();
            m_writer.Write(2);
            m_writer.Write(x);
            m_writer.Write(y);
            m_writer.Write(dx);
            m_writer.Write(dy);
            m_writer.Write(button);
            sendMessage();
        }

        public void mouseUp(int x, int y, int button)
        {
            startMessage();
            m_writer.Write(3);
            m_writer.Write(x);
            m_writer.Write(y);
            m_writer.Write(button);
            sendMessage();
        }


        public void navigate(float forward, float right, int fast)
        {
            startMessage();
            m_writer.Write(5);
            m_writer.Write(forward);
            m_writer.Write(right);
            m_writer.Write(fast);
            sendMessage();
        }

        public void setPosition(int entity, float x, float y, float z)
        {
            startMessage();
            m_writer.Write(14);
            m_writer.Write(entity);
            m_writer.Write(x);
            m_writer.Write(y);
            m_writer.Write(z);
            m_writer.Write(0);
            sendMessage();
        }

        public void createEntity()
        {
            startMessage();
            m_writer.Write(11);
            sendMessage();
        }

        public void removeComponent(uint cmp)
        {
            startMessage();
            m_writer.Write(10);
            m_writer.Write(cmp);
            m_writer.Write(0);
            sendMessage();
        }

        public void removeEntity()
        {
            startMessage();
            m_writer.Write(15);
            sendMessage();
        }

        public void editScript()
        {
            startMessage();
            m_writer.Write(17);
            sendMessage();
        }

        public void lookAtSelected()
        {
            startMessage();
            m_writer.Write(20);
            sendMessage();
        }

        public void startGameMode()
        {
            startMessage();
            m_writer.Write(12);
            sendMessage();
        }

        public void newUniverse()
        {
            startMessage();
            m_writer.Write(19);
            sendMessage();
        }

        public void saveUniverseAs(string filename)
        {
            startMessage();
            m_writer.Write(6);
            m_writer.Write(System.Text.Encoding.ASCII.GetBytes(filename));
            m_writer.Write(0);
            sendMessage();
        }

        public void reloadScript(string path)
        {
            startMessage();
            m_writer.Write(18);
            m_writer.Write(System.Text.Encoding.ASCII.GetBytes(path));
            m_writer.Write(0);
            sendMessage();
        }

        public void requestComponentProperties(uint cmp)
        {
            startMessage();
            m_writer.Write(9);
            m_writer.Write(cmp);
            m_writer.Write(0);
            sendMessage();
        }

        private void componentPropertiesCallback(BinaryReader reader)
        {
            int count = reader.ReadInt32();
            uint cmp_type = reader.ReadUInt32();
            string[] names = new string[count];
            string[] values = new string[count];
            uint[] types = new uint[count];
            for (int i = 0; i < count; ++i)
            {
                names[i] = readString(reader);
                values[i] = readString(reader);
                types[i] = reader.ReadUInt32();
            }
            if (onComponentProperties != null)
            {
                ComponentPropertiesArgs args = new ComponentPropertiesArgs();
                args.cmp_type = cmp_type;
                args.names = names;
                args.values = values;
                args.types = types;
                onComponentProperties(this, args);
            }
        }

        private string readString(BinaryReader reader)
        {
            int size = reader.ReadInt32();
            char[] str = new char[size];
            reader.Read(str, 0, size);
            return new string(str);
        }

        private void entitySelectedCallback(BinaryReader reader)
        {
            int entity_id = reader.ReadInt32();
            if (entity_id == -1)
            {
                if (onEntitySelected != null)
                {
                    EntitySelectedArgs args = new EntitySelectedArgs();
                    args.cmps = null;
                    args.uid = entity_id;
                    onEntitySelected(this, args);
                }
            }
            else
            {
                int component_count = reader.ReadInt32();
                uint[] types = new uint[component_count];
                for (int i = 0; i < component_count; ++i)
                {
                    types[i] = reader.ReadUInt32();
                }
                if (onEntitySelected != null)
                {
                    EntitySelectedArgs args = new EntitySelectedArgs();
                    args.cmps = types;
                    args.uid = entity_id;
                    onEntitySelected(this, args);
                }
            }
        }

        public event EventHandler onLogMessage;
        public class LegMessageEventArgs : EventArgs
        {
            public string system;
            public string message;
            public int type;
        }

        private void logMessage(BinaryReader reader)
        {
            LegMessageEventArgs e = new LegMessageEventArgs();
            e.type = reader.ReadInt32();
            e.system = readString(reader);
            e.message = readString(reader);
            if (onLogMessage != null)
            {
                onLogMessage(this, e);
            }
        }


        private void entityPositionCallback(BinaryReader reader)
        {
            int uid = reader.ReadInt32();
            float x = reader.ReadSingle();
            float y = reader.ReadSingle();
            float z = reader.ReadSingle();
            if (onEntityPosition != null)
            {
                EntityPositionArgs args = new EntityPositionArgs();
                args.x = x;
                args.y = y;
                args.z = z;
                args.uid = uid;
                onEntityPosition(this, args);
            }
        }


        public void editorServerCallback(IntPtr ptr, int size)
        {
            if (ptr != IntPtr.Zero)
            {
                byte[] bytes = new byte[size];
                Marshal.Copy(ptr, bytes, 0, size);
                using (MemoryStream memory = new MemoryStream(bytes))
                {
                    using (BinaryReader reader = new BinaryReader(memory))
                    {
                        int type = reader.ReadInt32();
                        switch (type)
                        {
                            case 1:
                                entitySelectedCallback(reader);
                                break;
                            case 2:
                                componentPropertiesCallback(reader);
                                break;
                            case 3:
                                entityPositionCallback(reader);
                                break;
                            case 4:
                                logMessage(reader);
                                break;
                        }
                    }
                }
            }
        }
    }
}
