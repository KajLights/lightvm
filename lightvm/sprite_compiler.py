"""PixelArray and function-based sprite lowering for the portable frontend.

Callbacks become bounded bytecode control flow, including normal VM yielding.
Only pixel kernels and instance storage cross the native boundary.
"""
import ast
from .compiler_core import I, F, C, B, LOCAL, STATE, CompileError

IMAGE_FORMATS = {'rgb8':14, 'rgba8':15, 'rgbw8':16, 'rgbwa8':17}
CHANNELS = {'r':0, 'g':1, 'b':2, 'w':3, 'a':4}
FEATURE_NATIVES = {
    'channel_has': (27,B,(6,I),0), 'channel_set': (28,0,(6,I,I,I,I),0),
    'alpha_set': (29,0,(6,F,I,I),0), 'alpha_multiply': (30,0,(6,F,I,I),0),
    'image_draw': (31,0,(6,6,6,I),0), 'style_geometry': (32,0,(6,F,F,F),1),
    'style_color': (33,0,(6,F,F,C,F,F),1), 'image_fill': (34,0,(6,C,I,I,I),0),
    'image_copy': (35,0,(6,6),0), 'image_transform': (36,0,(6,6,I,I),0),
    'batch_reserve': (37,I,(6,F),1), 'batch_next': (38,I,(6,I),0),
    'batch_remove': (39,0,(6,I),1), 'batch_get_i': (40,I,(6,I,I),0),
    'batch_get_f': (41,F,(6,I,I),0), 'batch_set_i': (42,0,(6,I,I,I),1),
    'batch_set_f': (43,0,(6,I,I,F),1), 'batch_time': (44,F,(6,I,F),0),
    'batch_active': (45,I,(6,),0), 'batch_alive': (46,B,(6,I),0),
    'batch_delta': (47,F,(6,I,F),1),
    'image_gradient': (48,0,(6,C,C),0), 'image_colorwheel': (49,0,(6,F,F),0),
    'image_palette': (50,0,(6,6,F,F),0), 'image_point': (51,0,(6,I,C,I),0),
    'image_draw_wrap': (52,0,(6,6,6,I),0),
}
GEOMETRY = ('translate','scale','origin')
COLOR = ('alpha','brightness','tint','hue','saturation')
DEFAULTS = (0.0,1.0,0.0,1.0,1.0,0xffffff,0.0,1.0)

class SpriteCompiler:
    def feature_setup(self):
        self.sprite_definitions, self.batches = {}, {}
        self.pending_batches, self.descriptions = [], {}
        self.sprite_context = None
        self.feature_used = False
        self.wrap_used = False
        self.style_ref = None
        self.artwork = {}

    def feature_native(self, name, args, node):
        self.feature_used = True
        return self.native(name,args,node)

    def constant_expr(self, value):
        return self.declarative(ast.Constant(value=value))

    def arguments(self, call, names, defaults=None):
        defaults = defaults or {}
        if len(call.args)>len(names): self.fail(call,'too many arguments')
        values=dict(zip(names,call.args))
        for k in call.keywords:
            if k.arg not in names or k.arg in values:self.fail(call,'unknown or duplicate argument')
            values[k.arg]=k.value
        for name in names:
            if name not in values:
                if name not in defaults:self.fail(call,'missing argument: '+name)
                values[name]=defaults[name] if isinstance(defaults[name],ast.AST) else ast.Constant(value=defaults[name])
        return values

    def overflow(self, node):
        if not isinstance(node,ast.Constant) or node.value not in ('clip','wrap'):
            self.fail(node,'overflow must be clip or wrap')
        if node.value=='wrap':self.wrap_used=True
        return 'image_draw_wrap' if node.value=='wrap' else 'image_draw'

    def image_declaration(self, target, call, node, module=False):
        name=self.declaration_key(target)
        if not name or not module and (self.stage!=0 or self.depth or self.call_stack):self.fail(node,'declare PixelArray directly in @init')
        if name.startswith('ctx.') and name[4:] in self.fields or name in self.env or name in self.artwork or name in self.imports:self.fail(node,'duplicate image binding')
        values=self.arguments(call,('data','format','dtype','readonly'),{'format':'rgba8','dtype':'uint8','readonly':False})
        try:format=ast.literal_eval(values['format']);dtype=ast.literal_eval(values['dtype']);readonly=ast.literal_eval(values['readonly'])
        except (ValueError,TypeError):self.fail(node,'format, dtype and readonly must be literals')
        if not isinstance(format,str) or format not in IMAGE_FORMATS or dtype!='uint8' or type(readonly)!=bool:self.fail(node,'PixelArray requires rgb8/rgba8/rgbw8/rgbwa8, uint8 and boolean readonly')
        if module and not readonly:self.fail(node,'module artwork must be readonly; put mutable images in @init')
        kind=IMAGE_FORMATS[format];stride={14:3,15:4,16:4,17:5}[kind]
        data=values['data'];offset=len(self.table);size=0
        if isinstance(data,(ast.List,ast.Tuple)):
            try:raw=bytes(ast.literal_eval(data))
            except (ValueError,TypeError):self.fail(node,'PixelArray data must contain byte literals')
            if not raw or len(raw)%stride:self.fail(node,'image byte count must match its format')
            size=len(raw);length=self.constant_expr(size//stride)
            self.table.extend(int.from_bytes(raw[i:i+3],'little') for i in range(0,size,3))
        else:
            if readonly:self.fail(node,'readonly images require literal artwork')
            length=self.declarative(data)
            if self.expressions[length][1]!=I:self.fail(node,'pixel count must be an integer')
        reg=self.add_resource(name,kind,length,self.resources[0][2],self.constant_expr(offset),self.constant_expr(size))
        # Image initialization is encoded in the package; readonly artwork stays
        # there and is shared instead of copied into every VM instance.
        self.resources[-1]=(*self.resources[-1][:-1],2 if readonly else 0)
        if name.startswith('ctx.'):self.fields[name[4:]]=reg
        elif module:self.artwork[name]=reg
        else:self.env[name]=reg
        self.feature_used=True

    def sprite_definition(self,target,call,node):
        if not isinstance(target,ast.Name):self.fail(node,'Sprite definitions need a module name')
        args=self.arguments(call,('width','init','render'),{'init':None})
        try:width=ast.literal_eval(args['width'])
        except (ValueError,TypeError):self.fail(node,'sprite width must be a positive literal')
        if type(width)!=int or not 1<=width<=65535:self.fail(node,'invalid sprite width')
        callbacks={}
        for role in ('init','render'):
            n=args[role]
            if role=='init' and isinstance(n,ast.Constant) and n.value is None:callbacks[role]=None;continue
            if not isinstance(n,ast.Name) or n.id not in self.helpers:self.fail(node,'Sprite callbacks must name user functions declared earlier')
            fn=self.helpers[n.id]
            if len(fn.args.args)<(2 if role=='init' else 3) or role=='render' and len(fn.args.args)!=3:self.fail(node,'sprite callbacks take (s, runtime, ...args) and (s, runtime, pixels)')
            callbacks[role]=fn
        if target.id in self.sprite_definitions or target.id in self.imports:self.fail(node,'duplicate sprite definition')
        self.sprite_definitions[target.id]={'width':width,**callbacks}
        self.feature_used=True

    def batch_declaration(self,target,call,node):
        args=self.arguments(call,('sprite','capacity'))
        definition=self.sprite_definitions.get(getattr(args['sprite'],'id',''))
        if not definition:self.fail(node,'SpriteBatch requires a user-defined Sprite and capacity')
        name=self.declaration_key(target)
        if not name or self.stage!=0 or self.depth or self.call_stack:self.fail(node,'declare SpriteBatch directly in @init')
        if name.startswith('ctx.') and name[4:] in self.fields or name in self.env:self.fail(node,'duplicate batch binding')
        length=self.declarative(args['capacity'])
        if self.expressions[length][1]!=I:self.fail(node,'capacity must be an integer')
        fields={'__draw_'+k:(i,C if k=='tint' else F) for i,k in enumerate(GEOMETRY+COLOR)}
        ref=self.add_resource(name,18,length,self.constant_expr(0),self.constant_expr(8),self.constant_expr(223))
        scratch=self.add_resource(name+'.canvas',15,self.constant_expr(definition['width']),self.resources[0][2])
        self.batches[ref]={'definition':definition,'fields':fields,'scratch':scratch,'ref':ref,'initialized':definition['init'] is None}
        if name.startswith('ctx.'):self.fields[name[4:]]=ref
        else:self.env[name]=ref
        self.feature_used=True

    def channel(self,node):
        if not isinstance(node,ast.Constant) or node.value not in CHANNELS:self.fail(node,'channel must be r, g, b, w or a')
        return self.literal(CHANNELS[node.value])

    def image_range(self,call,ref,leading):
        values=self.arguments(call,tuple(leading)+('start','stop'),{'start':0,'stop':None})
        start=self.expr(values.pop('start'));stop=values.pop('stop')
        if isinstance(stop,ast.Constant) and stop.value is None:
            end=self.reg(I);self.emit('LENGTH',end,ref)
        else:end=self.expr(stop)
        if self.typ(start)!=I or self.typ(end)!=I:self.fail(call,'pixel range must use integer indices')
        return values,start,end

    def style(self,node):
        if self.style_ref is None:self.style_ref=self.add_resource('__style',19,self.constant_expr(1),self.constant_expr(0))
        return self.style_ref

    def description(self,node,kind):
        defaults=dict(zip(GEOMETRY+COLOR,DEFAULTS))
        names=GEOMETRY if kind=='Transform' else COLOR
        if node is None:return [self.colour_constant(defaults[k]) if k=='tint' else self.literal(defaults[k]) for k in names]
        key=self.declaration_key(node)
        if key in self.descriptions:
            saved_kind,regs=self.descriptions[key]
            if saved_kind!=kind:self.fail(node,'wrong transformation type')
            return regs
        if not isinstance(node,ast.Call) or self.imports.get(getattr(node.func,'id',''))!=kind:self.fail(node,'expected '+kind)
        args=self.arguments(node,names,{k:defaults[k] for k in names})
        return [self.colour_constant(defaults[k]) if k=='tint' and isinstance(args[k],ast.Constant) and args[k].value==defaults[k] else self.convert(self.expr(args[k]),C if k=='tint' else F) for k in names]

    def set_style(self,geometry,color,node):
        ref=self.style(node)
        self.feature_native('style_geometry',[ref]+geometry,node)
        self.feature_native('style_color',[ref]+color,node)
        return ref

    def image_call(self,call,ref,kind):
        method=call.func.attr
        if method=='clear':
            self.check_write(ref,call)
            _,start,stop=self.image_range(call,ref,())
            return self.feature_native('image_fill',[ref,self.colour_constant(0),self.literal(0),start,stop],call)
        if method=='has_channel':
            args=self.arguments(call,('channel',));return self.feature_native('channel_has',[ref,self.channel(args['channel'])],call)
        if method in ('set_channel','set_alpha','multiply_alpha'):
            self.check_write(ref,call)
            args,start,stop=self.image_range(call,ref,('channel','value') if method=='set_channel' else ('value',))
            if method=='set_channel':
                value=self.expr(args['value'])
                if self.typ(value)!=I:self.fail(call,'channel values must be integer bytes')
                return self.feature_native('channel_set',[ref,self.channel(args['channel']),value,start,stop],call)
            return self.feature_native('alpha_set' if method=='set_alpha' else 'alpha_multiply',[ref,self.expr(args['value']),start,stop],call)
        if method=='draw':
            self.check_write(ref,call)
            args=self.arguments(call,('image','transform','color','sampling','overflow'),{'transform':None,'color':None,'sampling':'linear','overflow':'clip'})
            source,source_kind=self.resource(args['image'])
            if source_kind not in (3,12,14,15,16,17):self.fail(call,'draw requires a PixelArray')
            geometry=self.description(None if isinstance(args['transform'],ast.Constant) and args['transform'].value is None else args['transform'],'Transform')
            color=self.description(None if isinstance(args['color'],ast.Constant) and args['color'].value is None else args['color'],'ColorTransform')
            sampling=args['sampling']
            if not isinstance(sampling,ast.Constant) or sampling.value not in ('nearest','linear'):self.fail(call,'sampling must be nearest or linear')
            style=self.set_style(geometry,color,call)
            return self.feature_native(self.overflow(args['overflow']),[ref,source,style,self.literal(int(sampling.value=='linear'))],call)
        if method=='copy':
            self.check_write(ref,call);args=self.arguments(call,('source',));src,_=self.resource(args['source'])
            return self.feature_native('image_copy',[ref,src],call)
        if method=='transform':
            self.check_write(ref,call);args,start,stop=self.image_range(call,ref,('color',))
            style=self.set_style(self.description(None,'Transform'),self.description(args['color'],'ColorTransform'),call)
            return self.feature_native('image_transform',[ref,style,start,stop],call)
        if method in ('gradient','colorwheel','palette','point') and kind>=14:
            self.check_write(ref,call)
            if method=='point':
                args=self.arguments(call,('index','color','alpha'),{'alpha':255})
                return self.feature_native('image_point',[ref]+[self.expr(args[k]) for k in ('index','color','alpha')],call)
            if method=='palette':
                args=self.arguments(call,('colors','phase','stride'))
                source,_=self.resource(args['colors'])
                return self.feature_native('image_palette',[ref,source,self.expr(args['phase']),self.expr(args['stride'])],call)
            names=('first','last') if method=='gradient' else ('phase','stride')
            args=self.arguments(call,names)
            return self.feature_native('image_'+method,[ref]+[self.expr(args[k]) for k in names],call)
        if method=='fill' and kind>=14:
            self.check_write(ref,call)
            args=self.arguments(call,('color','alpha','start','stop'),{'alpha':255,'start':0,'stop':None})
            stop=args['stop']
            end=self.reg(I);self.emit('LENGTH',end,ref)
            start=self.expr(args['start'])
            if not (isinstance(stop,ast.Constant) and stop.value is None):end=self.expr(stop)
            if self.typ(start)!=I or self.typ(end)!=I:self.fail(call,'pixel range must use integer indices')
            return self.feature_native('image_fill',[ref,self.expr(args['color']),self.expr(args['alpha']),start,end],call)
        return None

    def sprite_get(self,name,node):
        batch,slot,role=self.sprite_context
        if name not in batch['fields']:self.fail(node,'unknown sprite state field: '+name)
        index,typ=batch['fields'][name]
        out=self.feature_native('batch_get_f' if typ==F else 'batch_get_i',[batch['ref'],slot,self.literal(index)],node)
        return self.truth(out) if typ==B else self.convert(out,C) if typ==C else out

    def sprite_set(self,name,value,node):
        batch,slot,role=self.sprite_context
        typ=self.typ(value)
        if name not in batch['fields']:
            if role!='init' or self.depth:self.fail(node,'initialize sprite fields unconditionally in its init callback')
            if typ not in (I,F,C,B) or len(batch['fields'])>=24:self.fail(node,'sprite supports up to 24 scalar fields including transforms')
            batch['fields'][name]=(len(batch['fields']),typ)
        index,expected=batch['fields'][name]
        if expected!=typ:self.fail(node,'sprite field changes type')
        if typ==C:value=self.convert(value,I)
        if typ==B:
            out=self.reg(I);j=self.emit('JZ',value);self.emit('MOV',out,self.literal(1));end=self.emit('JMP');self.code[j][4]=len(self.code);self.emit('MOV',out,self.literal(0));self.code[end][4]=len(self.code);value=out
        self.feature_native('batch_set_f' if typ==F else 'batch_set_i',[batch['ref'],slot,self.literal(index),value],node)

    def callback(self,batch,slot,role,args,now):
        fn=batch['definition'][role]
        if fn is None:return
        previous=(self.env,self.caps,self.inputs,self.output,self.loop_stack,self.depth,self.sprite_context,self.descriptions)
        self.env={};self.caps={fn.args.args[0].arg:'sprite',fn.args.args[1].arg:'runtime'}
        self.inputs={**self.inputs,'pixel_count':self.literal(batch['definition']['width'])}
        if role=='render':
            self.inputs['time']=self.feature_native('batch_time',[batch['ref'],slot,now],fn)
            self.inputs['delta']=self.feature_native('batch_delta',[batch['ref'],slot,now],fn)
            self.caps[fn.args.args[2].arg]='pixels';self.output=batch['scratch']
        else:
            self.inputs['time']=self.literal(0.0);self.inputs['delta']=self.literal(0.0)
            for formal,actual in zip(fn.args.args[2:],args):
                local=self.reg(self.typ(actual));self.emit('MOV',local,actual);self.env[formal.arg]=local
        self.loop_stack=[];self.depth=0;self.sprite_context=(batch,slot,role);self.descriptions={}
        returns=[];self.sprite_returns=returns
        self.statements(fn.body)
        for jump in returns:self.code[jump][4]=len(self.code)
        self.env,self.caps,self.inputs,self.output,self.loop_stack,self.depth,self.sprite_context,self.descriptions=previous
        self.sprite_returns=[]

    def batch_call(self,call,ref):
        if self.sprite_context:self.fail(call,'sprite callbacks cannot mutate or render a batch recursively')
        batch=self.batches[ref];method=call.func.attr
        if method=='spawn':
            fn=batch['definition']['init'];names=tuple(a.arg for a in fn.args.args[2:]) if fn else ()
            args=self.arguments(call,names)
            values=[self.expr(args[name]) for name in names]
            slot=self.feature_native('batch_reserve',[ref,self.inputs['time']],call)
            out=self.reg(B);cond=self.reg(B);self.emit('ILT',cond,slot,self.literal(0));self.emit('EQ',out,cond,self.literal(False))
            jump=self.emit('JZ',out)
            self.callback(batch,slot,'init',values,self.inputs['time'])
            batch['initialized']=True
            self.code[jump][4]=len(self.code)
            return out
        if method=='render':
            args=self.arguments(call,('runtime','pixels','overflow','sampling'),{'overflow':'clip','sampling':'linear'})
            if self.capability(args['runtime'])!='runtime':self.fail(call,'batch.render(runtime, pixels) expected')
            dst,kind=self.resource(args['pixels']);self.check_write(dst,call)
            if kind not in (3,12,14,15,16,17):self.fail(call,'batch destination must be pixels')
            binding=self.overflow(args['overflow'])
            sampling=args['sampling']
            if not isinstance(sampling,ast.Constant) or sampling.value not in ('linear','nearest'):self.fail(call,'sampling must be linear or nearest')
            jump=self.emit('JMP');self.pending_batches.append((jump,len(self.code),batch,dst,self.inputs['time'],dict(self.env),dict(self.caps),dict(self.inputs),self.output,binding,int(sampling.value=='linear')))
            return -1
        self.fail(call,'SpriteBatch supports spawn(), render() and active')

    def finish_batches(self):
        for jump,back,batch,dst,now,env,caps,inputs,output,binding,sampling in self.pending_batches:
            if not batch['initialized']:raise CompileError('sprite init needs a spawn call to establish its field types')
            self.env,self.caps,self.inputs,self.output=env,caps,inputs,output
            self.code[jump][4]=len(self.code)
            slot=self.reg(I);self.emit('MOV',slot,self.literal(0));head=len(self.code)
            found=self.feature_native('batch_next',[batch['ref'],slot],None);self.emit('MOV',slot,found)
            done=self.reg(B);self.emit('ILT',done,slot,self.literal(0));body=self.emit('JZ',done);leave=self.emit('JMP');self.code[body][4]=len(self.code)
            scratch=batch['scratch'];length=self.literal(batch['definition']['width'])
            self.feature_native('image_fill',[scratch,self.colour_constant(0),self.literal(0),self.literal(0),length],None)
            self.callback(batch,slot,'render',[],now)
            alive=self.feature_native('batch_alive',[batch['ref'],slot],None);skip=self.emit('JZ',alive)
            self.sprite_context=(batch,slot,'render')
            geometry=[self.sprite_get("__draw_"+k,None) for k in GEOMETRY];color=[self.sprite_get("__draw_"+k,None) for k in COLOR]
            self.sprite_context=None
            style=self.set_style(geometry,color,None)
            self.feature_native(binding,[dst,scratch,style,self.literal(sampling)],None)
            self.code[skip][4]=len(self.code)
            self.emit('IADD',slot,slot,self.literal(1));self.emit('JMP',imm=head)
            self.code[leave][4]=len(self.code);self.emit('JMP',imm=back)
        self.pending_batches=[]
        for batch in self.batches.values():
            mask=sum(1<<index for index,typ in batch['fields'].values() if typ==F)
            identity=self.regs[batch['ref']][2];kind,length,fill,_,_,flags=self.resources[identity]
            self.resources[identity]=(kind,length,fill,self.constant_expr(len(batch['fields'])),self.constant_expr(mask),flags)

    def feature_expr(self,node):
        if isinstance(node,ast.Name) and node.id in self.artwork:return self.artwork[node.id]
        if isinstance(node,ast.Attribute):
            if isinstance(node.value,ast.Name) and node.value.id in self.sprite_definitions and node.attr=='width':
                return self.literal(self.sprite_definitions[node.value.id]['width'])
            if self.capability(node.value)=='sprite':return self.sprite_get(node.attr,node)
            if node.attr in ('count','active'):
                try:ref,kind=self.resource(node.value)
                except CompileError:return None
                if node.attr=='active' and kind==18:return self.feature_native('batch_active',[ref],node)
                if node.attr=='count' and kind in (14,15,16,17):out=self.reg(I);self.emit('LENGTH',out,ref);return out
        if not isinstance(node,ast.Call) or not isinstance(node.func,ast.Attribute):return None
        if self.capability(node.func.value)=='sprite' and node.func.attr=='remove':
            if node.args or node.keywords:self.fail(node,'remove takes no arguments')
            batch,slot,_=self.sprite_context;return self.feature_native('batch_remove',[batch['ref'],slot],node)
        try:ref,kind=self.resource(node.func.value)
        except CompileError:return None
        if kind==18:return self.batch_call(node,ref)
        if kind in (3,12,14,15,16,17):return self.image_call(node,ref,kind)
        return None

    def feature_assignment(self,target,value,node):
        if isinstance(value,ast.Call) and self.imports.get(getattr(value.func,'id','')) in ('Transform','ColorTransform'):
            kind=self.imports[value.func.id];regs=self.description(value,kind)
            if isinstance(target,ast.Attribute) and self.capability(target.value)=='sprite':
                if target.attr!=('transform' if kind=='Transform' else 'color'):self.fail(node,'assign Transform to s.transform and ColorTransform to s.color')
                for key,reg in zip(GEOMETRY if kind=='Transform' else COLOR,regs):self.sprite_set('__draw_'+key,reg,node)
            else:
                key=self.declaration_key(target)
                if not key:self.fail(node,'transforms need a local or context binding')
                previous=self.descriptions.get(key)
                if previous and previous[0]!=kind:self.fail(node,'transform binding changes type')
                if previous:dest=previous[1]
                else:
                    if self.depth:self.fail(node,'create transform bindings unconditionally')
                    if key.startswith('ctx.') and key[4:] in self.fields:self.fail(node,'context field already exists')
                    if key.startswith('ctx.') and (self.stage!=0 or self.depth):self.fail(node,'initialize context transforms in @init')
                    dest=[self.reg(self.typ(r),STATE if key.startswith('ctx.') else LOCAL) for r in regs]
                for dst,src in zip(dest,regs):self.emit('MOV',dst,src)
                self.descriptions[key]=(kind,dest)
            self.feature_used=True;return True
        return False
