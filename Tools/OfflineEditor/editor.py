import argparse
import copy
from pathlib import Path
import queue
import threading
import time
import tkinter as tk
from tkinter import ttk, messagebox, filedialog
from PIL import Image, ImageTk
from data import GameData, CharacterStore, LiveBridge, RACES, CLASSES, CONTINENTS, client_folder, client_folders, client_running

FIELDS = [('skin','Skin tone'),('face','Face'),('hairStyle','Hairstyle'),('hairColor','Hair color'),('facialHair','Facial hair / details')]

def variant(i):
    text=''
    while True:
        text=chr(65+i%26)+text; i=i//26-1
        if i<0: return text

class Editor:
    def __init__(self, root, project, character_file=None, client=None):
        self.root=root; self.project=Path(project); self.game=None; self.dirty=False; self.store=None
        # The game folder (dist-play when it exists, else dist; --client overrides). Settings are in
        # its User\Settings folder, the offline characters in WowUnreal\Saved.
        try: self.client=client_folder(project,client)
        except FileNotFoundError: self.client=Path(project)/(client or 'dist-play')
        self.bridge=LiveBridge(project,[self.client]+[c for c in client_folders(project) if c!=self.client]); self.bridge.enable(); self.live_request=None; self.live_started=0
        self.position=None; self.zone=None; self.map_original=None; self.map_photo=None; self.face_photo=None
        self.char=None; self.index=0; self.options={}; self.values={}; self.widgets={}; self.mapped_zones=[]
        self.character_file=Path(character_file) if character_file else self.client/'WowUnreal/Saved/OfflineCharacters.json'
        root.title('WoWUnreal • Offline test tool'); root.geometry('1240x900'); root.minsize(1060,840)
        root.configure(bg='#151c24'); root.protocol('WM_DELETE_WINDOW',self.close)
        style=ttk.Style(root); style.theme_use('clam')
        style.configure('.',font=('Segoe UI',10),background='#1c2631',foreground='#e4ebf2')
        style.configure('TFrame',background='#1c2631'); style.configure('TLabel',background='#1c2631')
        style.configure('TButton',padding=(12,7),background='#344557',foreground='white')
        style.map('TButton',background=[('active','#465e75')])
        style.configure('Accent.TButton',background='#267e70',font=('Segoe UI Semibold',11))
        style.configure('TCombobox',fieldbackground='#283644',background='#344557',foreground='white',padding=5)
        style.map('TCombobox',fieldbackground=[('readonly','#283644')],foreground=[('readonly','white')])
        style.configure('TEntry',fieldbackground='#283644',foreground='white',padding=5)
        style.configure('Title.TLabel',font=('Segoe UI Semibold',22))
        style.configure('Muted.TLabel',foreground='#a7b8c9')
        root.option_add('*TCombobox*Listbox.background','#283644'); root.option_add('*TCombobox*Listbox.foreground','white')
        self.shell=ttk.Frame(root,padding=22); self.shell.pack(fill='both',expand=True)
        ttk.Label(self.shell,text='Offline test tool',style='Title.TLabel').pack(anchor='w')
        ttk.Label(self.shell,text='Private character editor  ·  Save a starting location or move the active offline character live',style='Muted.TLabel').pack(anchor='w',pady=(2,16))
        self.status=tk.StringVar(value='Loading maps and appearance options from the Data folder …')
        self.body=ttk.Frame(self.shell); self.body.pack(fill='both',expand=True)
        bottom=ttk.Frame(self.shell); bottom.pack(fill='x',pady=(12,0))
        self.save_button=ttk.Button(bottom,text='Save character',style='Accent.TButton',command=self.save,state='disabled'); self.save_button.pack(side='right')
        ttk.Label(bottom,textvariable=self.status,wraplength=860,style='Muted.TLabel').pack(side='left',fill='x',expand=True)
        self.results=queue.Queue()
        def load():
            try: self.results.put(GameData(self.project,str(self.client)))
            except Exception as e: self.results.put(e)
        threading.Thread(target=load,daemon=True).start(); root.after(100,self.loaded)

    def loaded(self):
        try: result=self.results.get_nowait()
        except queue.Empty: self.root.after(100,self.loaded); return
        if isinstance(result,Exception):
            self.status.set('Could not open game data.'); messagebox.showerror('Game data',str(result)); return
        self.game=result; self.make_form(); self.reload()

    def combo(self,parent,key,title,values,callback):
        ttk.Label(parent,text=title).pack(anchor='w',pady=(8,3))
        var=tk.StringVar(); self.values[key]=var
        combo=ttk.Combobox(parent,textvariable=var,values=values,state='readonly',width=29)
        combo.pack(fill='x'); combo.bind('<<ComboboxSelected>>',callback); self.widgets[key]=combo
        return combo

    def make_form(self):
        left=ttk.Frame(self.body,width=280); left.pack(side='left',fill='y',padx=(0,22)); left.pack_propagate(False)
        self.combo(left,'character','Offline character',[],self.select_character)
        row=ttk.Frame(left); row.pack(fill='x',pady=8)
        ttk.Button(row,text='Reload',command=self.reload).pack(side='left')
        ttk.Button(row,text='Choose file …',command=self.open_file).pack(side='right')
        ttk.Label(left,text='Name').pack(anchor='w',pady=(10,3))
        self.values['name']=tk.StringVar(); entry=ttk.Entry(left,textvariable=self.values['name']); entry.pack(fill='x')
        entry.bind('<KeyRelease>',lambda e:self.changed())
        self.combo(left,'race','Race',list(RACES.values()),self.identity_changed)
        self.combo(left,'gender','Gender',['Male','Female'],self.identity_changed)
        ttk.Label(left,text='Character class is preserved',style='Muted.TLabel').pack(anchor='w',pady=(8,0))
        self.class_label=ttk.Label(left,text=''); self.class_label.pack(anchor='w')
        for key,title in FIELDS: self.combo(left,key,title,[],lambda e,k=key:self.appearance_changed(k))
        self.preview=ttk.Label(left,text=''); self.preview.pack(pady=(12,0))
        ttk.Label(left,text='Face texture from game data.\nThe full model is shown in the client.',style='Muted.TLabel',justify='center').pack()
        right=ttk.Frame(self.body); right.pack(side='left',fill='both',expand=True)
        filters=ttk.Frame(right); filters.pack(fill='x')
        continent=ttk.Frame(filters); continent.pack(side='left',fill='x',expand=True,padx=(0,12))
        self.combo(continent,'continent','Continent',list(CONTINENTS.values()),self.continent_changed)
        zones=ttk.Frame(filters); zones.pack(side='left',fill='x',expand=True)
        self.combo(zones,'zone','Zone',[],self.zone_changed)
        shortcuts=ttk.Frame(right); shortcuts.pack(fill='x',pady=12)
        ttk.Button(shortcuts,text='Go to Northshire',command=self.northshire).pack(side='left')
        ttk.Button(shortcuts,text='Show character location',command=self.show_current).pack(side='left',padx=8)
        self.live_button=ttk.Button(shortcuts,text='Move here now',style='Accent.TButton',command=self.teleport,state='disabled'); self.live_button.pack(side='right')
        # r528 (Stefan: no way up onto Stormwind's roofs or wall offline): fly the live offline character.
        self.fly_var=tk.BooleanVar(value=False); self.fly_sent=0.0
        self.fly_check=ttk.Checkbutton(shortcuts,text='Fly  (Jump rises, swim-down key sinks)',variable=self.fly_var,command=self.toggle_fly,state='disabled'); self.fly_check.pack(side='right',padx=12)
        self.live_status=tk.StringVar(value='Live: waiting for an offline client')
        ttk.Label(right,textvariable=self.live_status,style='Muted.TLabel').pack(anchor='w',pady=(0,8))
        self.root.after(500,self.poll_live)
        self.canvas=tk.Canvas(right,bg='#121920',highlightthickness=0,cursor='crosshair')
        self.canvas.pack(fill='both',expand=True); self.canvas.bind('<Configure>',lambda e:self.draw_map()); self.canvas.bind('<Button-1>',self.map_clicked)
        self.place=tk.StringVar(value='Click on land to choose a starting location.')
        ttk.Label(right,textvariable=self.place,font=('Segoe UI Semibold',12),wraplength=800).pack(anchor='w',pady=(12,4))
        ttk.Label(right,text='Map selection checks building floors and terrain. Choose a surface when several heights are available. Select an open spot; water and small objects are not included.',wraplength=800,style='Muted.TLabel').pack(anchor='w')
        self.file_label=ttk.Label(right,text='',wraplength=800,style='Muted.TLabel'); self.file_label.pack(anchor='w',pady=(12,0))

    def changed(self):
        self.dirty=True; self.save_button.configure(state='normal'); self.status.set('Unsaved changes.')

    def discard(self): return not self.dirty or messagebox.askyesno('Unsaved changes','Discard unsaved changes?')

    def reload(self):
        if not self.discard(): return
        try: self.store=CharacterStore(self.character_file)
        except Exception as e: messagebox.showerror('Offline characters',str(e)); return
        self.widgets['character']['values']=[f"{c['name']} · {RACES.get(c['race'],'Unknown race')}" for c in self.store.characters]
        if not self.store.characters:
            self.status.set('No offline characters. Create a character in the client first.'); return
        self.index=min(self.index,len(self.store.characters)-1); self.widgets['character'].current(self.index); self.load_character()
        self.file_label.configure(text='Character file: '+str(self.character_file))

    def open_file(self):
        if not self.discard(): return
        path=filedialog.askopenfilename(title='Choose OfflineCharacters.json',initialdir=self.character_file.parent,filetypes=[('Offline characters','OfflineCharacters.json')])
        if path: self.character_file=Path(path); self.dirty=False; self.reload()

    def select_character(self,event=None):
        new_index=self.widgets['character'].current()
        if not self.discard(): self.widgets['character'].current(self.index); return
        self.index=new_index; self.load_character()

    def load_character(self):
        self.char=copy.deepcopy(self.store.characters[self.index]); self.position={k:self.char[k] for k in ['map','zone','x','y','z']}
        self.values['name'].set(self.char['name']); self.values['race'].set(RACES.get(self.char['race'],''))
        self.values['gender'].set(['Male','Female'][self.char['gender']]); self.class_label.configure(text=CLASSES.get(self.char['class'],'Unknown'))
        self.refresh_appearance(); self.show_current(); self.dirty=False; self.save_button.configure(state='disabled')
        self.status.set('Ready. Choose a starting location on the map or change the character appearance.')

    def identity_changed(self,event=None):
        self.char['race']=next(k for k,v in RACES.items() if v==self.values['race'].get())
        self.char['gender']=self.widgets['gender'].current()
        for key,_ in FIELDS: self.char[key]=0
        self.refresh_appearance(); self.changed()

    def refresh_appearance(self):
        for key,title in FIELDS:
            opts=self.game.appearance(self.char,key); self.options[key]=opts
            if self.char.get(key) not in opts: self.char[key]=opts[0]
            labels=[f'Hairstyle {i+1}' if key=='hairStyle' else f'{title} {variant(i)}' for i in range(len(opts))]
            self.widgets[key]['values']=labels; self.widgets[key].current(opts.index(self.char[key]))
        try:
            im=self.game.face_texture(self.char)
            if im:
                im.thumbnail((100,75)); self.face_photo=ImageTk.PhotoImage(im)
                self.preview.configure(image=self.face_photo,text='')
            else: self.preview.configure(image='',text='No texture preview')
        except Exception: self.preview.configure(image='',text='Texture preview unavailable')

    def appearance_changed(self,key):
        self.char[key]=self.options[key][self.widgets[key].current()]; self.refresh_appearance(); self.changed()

    def continent_changed(self,event=None):
        map_id=next(k for k,v in CONTINENTS.items() if v==self.values['continent'].get())
        self.mapped_zones=[z for z in self.game.zones if z['map']==map_id]
        self.widgets['zone']['values']=[z['name'] for z in self.mapped_zones]
        self.widgets['zone'].current(0); self.zone_changed()

    def zone_changed(self,event=None):
        self.zone=self.mapped_zones[self.widgets['zone'].current()]
        self.status.set('Loading '+self.zone['name']+' …'); self.root.update_idletasks()
        try: self.map_original=self.game.map_image(self.zone)
        except Exception: self.map_original=None; self.status.set('Map artwork missing: '+self.zone['name'])
        self.draw_map(); self.update_place()
        if self.map_original: self.status.set('Click the map to move the character. Selecting a zone alone does not change the starting location.')

    def show_current(self):
        pos=self.position
        if pos['map'] not in CONTINENTS:
            self.values['continent'].set('Eastern Kingdoms'); self.continent_changed()
            self.status.set('Select a continent and click a new starting location. The character’s current map is not supported here.'); return
        self.values['continent'].set(CONTINENTS[pos['map']]); self.mapped_zones=[z for z in self.game.zones if z['map']==pos['map']]
        candidates=[]
        for i,z in enumerate(self.mapped_zones):
            u,v=self.game.map_uv(z,pos['x'],pos['y'])
            if 0<=u<=1 and 0<=v<=1: candidates.append((abs((z['bounds'][0]-z['bounds'][1])*(z['bounds'][2]-z['bounds'][3])),i))
        area=self.game.areas.get(pos['zone'],{}); parent=area.get('parent') or pos['zone']
        index=next((i for i,z in enumerate(self.mapped_zones) if z['area']==parent),min(candidates)[1] if candidates else 0)
        self.widgets['zone']['values']=[z['name'] for z in self.mapped_zones]; self.widgets['zone'].current(index); self.zone_changed()

    def draw_map(self):
        self.canvas.delete('all')
        if self.map_original is None:
            self.canvas.create_text(20,30,text='Map artwork unavailable for this zone.',fill='#a7b8c9',anchor='nw'); return
        w,h=self.canvas.winfo_width(),self.canvas.winfo_height()
        scale=min(w/1002,h/668); iw,ih=max(1,int(1002*scale)),max(1,int(668*scale))
        self.rect=((w-iw)/2,(h-ih)/2,iw,ih)
        self.map_photo=ImageTk.PhotoImage(self.map_original.resize((iw,ih),Image.Resampling.LANCZOS))
        ox,oy,_,_=self.rect; self.canvas.create_image(ox,oy,image=self.map_photo,anchor='nw')
        if self.position and self.position['map']==self.zone['map']:
            u,v=self.game.map_uv(self.zone,self.position['x'],self.position['y'])
            if 0<=u<=1 and 0<=v<=1:
                px,py=ox+u*iw,oy+v*ih
                self.canvas.create_oval(px-9,py-9,px+9,py+9,fill='#14342f',outline='white',width=2)
                self.canvas.create_oval(px-4,py-4,px+4,py+4,fill='#53e3bb',outline='')

    def map_clicked(self,event):
        if self.map_original is None: return
        ox,oy,w,h=self.rect; u,v=(event.x-ox)/w,(event.y-oy)/h
        if not(0<=u<=1 and 0<=v<=1): return
        x,y=self.game.map_position(self.zone,u,v)
        try:
            options=self.game.ground_options(self.zone['map'],x,y)
            selected=self.choose_surface(options)
            if selected is None: return
            self.position=selected
        except Exception as e: messagebox.showinfo('Choose another location',str(e)); return
        self.draw_map(); self.update_place(); self.changed()

    def choose_surface(self,options):
        if len(options)==1: return options[0]
        dialog=tk.Toplevel(self.root); dialog.title('Choose height'); dialog.transient(self.root)
        ttk.Label(dialog,text='Several floors or roofs exist here. The map cannot determine the intended floor.\nChoose a surface, or cancel and select an open street area.',padding=12).pack()
        choices=tk.Listbox(dialog,height=min(10,len(options)),exportselection=False)
        for i,p in enumerate(options): choices.insert('end',f'Surface {i+1} – height {p["z"]:.1f}')
        choices.pack(fill='x',padx=12); result=[]
        def accept():
            if choices.curselection(): result.append(options[choices.curselection()[0]]); dialog.destroy()
        ttk.Button(dialog,text='Use selected surface',command=accept).pack(pady=8)
        ttk.Button(dialog,text='Cancel',command=dialog.destroy).pack(pady=8)
        dialog.grab_set(); self.root.wait_window(dialog)
        return result[0] if result else None

    def update_place(self):
        if not self.position: return
        name=self.game.areas.get(self.position['zone'],{}).get('name') or 'Unknown area'
        self.place.set('Starting location: '+CONTINENTS.get(self.position['map'],'Other map')+'  /  '+name)

    def northshire(self):
        try: self.position=self.game.ground(0,-8949,-132)
        except Exception as e: messagebox.showerror('Northshire',str(e)); return
        self.show_current(); self.changed()

    def save(self):
        if not self.char: return
        name=self.values['name'].get().strip()
        if not name or len(name)>24 or any(ord(ch)<32 for ch in name):
            messagebox.showerror('Name','Use a name containing 1–24 characters.'); return
        chars=copy.deepcopy(self.store.characters); record=copy.deepcopy(self.char)
        record['name']=name; record.update(self.position); chars[self.index]=record
        if client_running() and not self.bridge.status() and not messagebox.askyesno('Client running',
                'The game is running but not connected to this tool. Saving is fine at the login screen and at character selection. '
                'If '+name+' is in the world right now, logging out will put back the old location. Save anyway?'): return
        guid=self.char['guid']
        try: self.store.save(chars,guard=lambda:self.bridge.editing_blocked(self.character_file,guid))
        except Exception as e: messagebox.showerror('Could not save',str(e)); return
        self.dirty=False; self.load_character()
        self.widgets['character']['values']=[f"{c['name']} · {RACES.get(c['race'],'Unknown race')}" for c in self.store.characters]; self.widgets['character'].current(self.index)
        self.status.set('Saved with a backup. Log in to the offline character to use the new location.')

    def teleport(self):
        if not self.char: return
        try: self.live_request=self.bridge.teleport(self.char['guid'],self.position)
        except Exception as e: messagebox.showinfo('Live move',str(e)); return
        self.live_started=time.monotonic(); self.live_button.configure(state='disabled')
        self.status.set('Sent to the offline client. Waiting for terrain and confirmation …')

    def toggle_fly(self):
        if not self.char: return
        try: self.bridge.fly(self.char['guid'],bool(self.fly_var.get()))
        except Exception as e: messagebox.showinfo('Fly',str(e)); self.fly_var.set(not self.fly_var.get()); return
        self.fly_sent=time.monotonic()
        self.status.set('Flying on: Jump rises, the swim-down key sinks. Untick to land.' if self.fly_var.get() else 'Flying off: the character lands where it is.')

    def poll_live(self):
        state=self.bridge.status()
        same=bool(state and self.char and state.get('guid')==self.char['guid'])
        self.fly_check.configure(state='normal' if same and state.get('ready') else 'disabled')
        if same and 'flying' in state and time.monotonic()-self.fly_sent>2: self.fly_var.set(bool(state['flying']))
        ready=bool(same and state.get('ready') and not self.live_request)
        self.live_button.configure(state='normal' if ready else 'disabled')
        self.live_status.set('Live: offline character connected' if same else 'Live: client at the login screen or character selection; saving is available here' if state and 'guid' not in state else 'Live: another offline character is in the world; saving is available here' if state else 'Live: no connection. Start a supported client in offline mode.')
        if self.live_request:
            if state and state.get('ack')==self.live_request:
                if state.get('error'):
                    self.status.set('Live move rejected: '+state['error']); self.live_request=None
                elif state.get('result')=='moved' and state.get('ready'):
                    self.status.set('Client confirmed the live move. The saved starting location is unchanged.'); self.live_request=None
                else: self.status.set('The client is loading terrain at the new location …')
            if self.live_request and time.monotonic()-self.live_started>60:
                self.status.set('No completion confirmation. Check the client before trying again.'); self.live_request=None
        self.root.after(500,self.poll_live)

    def close(self):
        if not self.discard(): return
        if self.game: self.game.mpq.close()
        self.root.destroy()

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--project',default='E:/WowUnreal'); parser.add_argument('--characters')
    parser.add_argument('--client',help='game folder (default: dist-play, else dist)')
    parser.add_argument('--check-runtime',action='store_true',help=argparse.SUPPRESS)
    args=parser.parse_args()
    if args.check_runtime:
        root=tk.Tk(); root.withdraw(); root.update_idletasks(); root.destroy(); return
    try:
        import ctypes
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except Exception: pass
    root=tk.Tk(); Editor(root,args.project,args.characters,args.client); root.mainloop()

if __name__=='__main__': main()
